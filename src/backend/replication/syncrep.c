/*-------------------------------------------------------------------------
 *
 * syncrep.c
 *
 * Synchronous replication is new as of PostgreSQL 9.1.
 *
 * If requested, transaction commits wait until their commit LSN are
 * acknowledged by the synchronous standbys.
 *
 * This module contains the code for waiting and release of backends.
 * All code in this module executes on the primary. The core streaming
 * replication transport remains within WALreceiver/WALsender modules.
 *
 * The essence of this design is that it isolates all logic about
 * waiting/releasing onto the primary. The primary defines which standbys
 * it wishes to wait for. The standbys are completely unaware of the
 * durability requirements of transactions on the primary, reducing the
 * complexity of the code and streamlining both standby operations and
 * network bandwidth because there is no requirement to ship
 * per-transaction state information.
 *
 * Replication is either synchronous or not synchronous (async). If it is
 * async, we just fastpath out of here. If it is sync, then we wait for
 * the write, flush or apply location on the standby before releasing
 * the waiting backend. Further complexity in that interaction is
 * expected in later releases.
 *
 * The best performing way to manage the waiting backends is to have a
 * single ordered queue of waiting backends, so that we can avoid
 * searching the through all waiters each time we receive a reply.
 *
 * In 9.5 or before only a single standby could be considered as
 * synchronous. In 9.6 we support a priority-based multiple synchronous
 * standbys. In 10.0 a quorum-based multiple synchronous standbys is also
 * supported. The number of synchronous standbys that transactions
 * must wait for replies from is specified in synchronous_standby_names.
 * This parameter also specifies a list of standby names and the method
 * (FIRST and ANY) to choose synchronous standbys from the listed ones.
 *
 * The method FIRST specifies a priority-based synchronous replication
 * and makes transaction commits wait until their WAL records are
 * replicated to the requested number of synchronous standbys chosen based
 * on their priorities. The standbys whose names appear earlier in the list
 * are given higher priority and will be considered as synchronous.
 * Other standby servers appearing later in this list represent potential
 * synchronous standbys. If any of the current synchronous standbys
 * disconnects for whatever reason, it will be replaced immediately with
 * the next-highest-priority standby.
 *
 * The method ANY specifies a quorum-based synchronous replication
 * and makes transaction commits wait until their WAL records are
 * replicated to at least the requested number of synchronous standbys
 * in the list. All the standbys appearing in the list are considered as
 * candidates for quorum synchronous standbys.
 *
 * If neither FIRST nor ANY is specified, FIRST is used as the method.
 * This is for backward compatibility with 9.6 or before where only a
 * priority-based sync replication was supported.
 *
 * Before the standbys chosen from synchronous_standby_names can
 * become the synchronous standbys they must have caught up with
 * the primary; that may take some time. Once caught up,
 * the standbys which are considered as synchronous at that moment
 * will release waiters from the queue.
 *
 * Portions Copyright (c) 2010-2019, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/replication/syncrep.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>

#include "access/xact.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "replication/syncrep.h"
#include "replication/walsender.h"
#include "replication/walsender_private.h"
#include "storage/pmsignal.h"
#include "storage/proc.h"
#include "storage/procsignal.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/ps_status.h"
#include "utils/faultinjector.h"
#include "pgstat.h"
#include "cdb/cdbvars.h"

/* User-settable parameters for sync rep */
char	   *SyncRepStandbyNames;

#define SyncStandbysDefined() \
	(SyncRepStandbyNames != NULL && SyncRepStandbyNames[0] != '\0')

static bool announce_next_takeover = true;

SyncRepConfigData *SyncRepConfig = NULL;
static int	SyncRepWaitMode = SYNC_REP_NO_WAIT;

static void SyncRepQueueInsert(int mode);
static void SyncRepCancelWait(void);

static bool SyncRepGetSyncRecPtr(XLogRecPtr *writePtr,
								 XLogRecPtr *flushPtr,
								 XLogRecPtr *applyPtr,
								 bool *am_sync);
static void SyncRepGetOldestSyncRecPtr(XLogRecPtr *writePtr,
									   XLogRecPtr *flushPtr,
									   XLogRecPtr *applyPtr,
									   List *sync_standbys);
static void SyncRepGetNthLatestSyncRecPtr(XLogRecPtr *writePtr,
										  XLogRecPtr *flushPtr,
										  XLogRecPtr *applyPtr,
										  List *sync_standbys, uint8 nth);
static int	SyncRepGetStandbyPriority(void);
static List *SyncRepGetSyncStandbysPriority(bool *am_sync);
static List *SyncRepGetSyncStandbysQuorum(bool *am_sync);
static int	cmp_lsn(const void *a, const void *b);

#ifdef USE_ASSERT_CHECKING
static bool SyncRepQueueIsOrderedByLSN(int mode);
#endif

/*
 * ===========================================================
 * Synchronous Replication functions for normal user backends
 * ===========================================================
 */

/*
 * Wait for synchronous replication, if requested by user.
 *
 * Initially backends start in state SYNC_REP_NOT_WAITING and then
 * change that state to SYNC_REP_WAITING before adding ourselves
 * to the wait queue. During SyncRepWakeQueue() a WALSender changes
 * the state to SYNC_REP_WAIT_COMPLETE once replication is confirmed.
 * This backend then resets its state to SYNC_REP_NOT_WAITING.
 *
 * 'lsn' represents the LSN to wait for.  'commit' indicates whether this LSN
 * represents a commit record.  If it doesn't, then we wait only for the WAL
 * to be flushed if synchronous_commit is set to the higher level of
 * remote_apply, because only commit records provide apply feedback.
 */
void
SyncRepWaitForLSN(XLogRecPtr lsn, bool commit)
{
	char	   *new_status = NULL;
	const char *old_status;
	int			mode;

	/* Cap the level for anything other than commit to remote flush only. */
	if (commit)
		mode = SyncRepWaitMode;
	else
		mode = Min(SyncRepWaitMode, SYNC_REP_WAIT_FLUSH);

	/*
	 * SIGUSR1 is used to wake us up, cannot wait from inside SIGUSR1 handler
	 * as its non-reentrant, so check for the same and avoid waiting.
	 */
	if (AmIInSIGUSR1Handler())
	{
		return;
	}
	Assert(!am_walsender);
	elogif(debug_walrepl_syncrep, LOG,
			"syncrep wait -- This backend's commit LSN for syncrep is %X/%X.",
		   (uint32) (lsn >> 32), (uint32) lsn);

	/*
	 * Fast exit if user has not requested sync replication.
	 */
	if (!SyncRepRequested())
		return;

	Assert(SHMQueueIsDetached(&(MyProc->syncRepLinks)));
	Assert(WalSndCtl != NULL);

	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
	Assert(MyProc->syncRepState == SYNC_REP_NOT_WAITING);

	if (IS_QUERY_DISPATCHER())
	{
		/*
		 * There could be a better way to figure out if there is any active
		 * standby.  But currently, let's move ahead by looking at the per WAL
		 * sender structure to see if anyone is really active, streaming (or
		 * still catching up within limits) and wants to be synchronous.
		 */
		bool		syncStandbyPresent = false;
		int			i;

		for (i = 0; i < max_wal_senders; i++)
		{
			/* use volatile pointer to prevent code rearrangement */
			volatile WalSnd *walsnd = &WalSndCtl->walsnds[i];

			SpinLockAcquire(&walsnd->mutex);
			syncStandbyPresent = (walsnd->pid != 0)
				&& ((walsnd->state == WALSNDSTATE_STREAMING)
					|| (walsnd->state == WALSNDSTATE_CATCHUP &&
						walsnd->caughtup_within_range));
			SpinLockRelease(&walsnd->mutex);

			if (syncStandbyPresent)
				break;
		}

		/* See if we found any active standby connected. If NO, no need to wait.*/
		if (!syncStandbyPresent)
		{
			elogif(debug_walrepl_syncrep, LOG,
					"syncrep wait -- Not waiting for syncrep because no active and synchronous "
					"standby (walsender) was found.");

			LWLockRelease(SyncRepLock);
			return;
		}
	}

	/*
	 * We don't wait for sync rep if WalSndCtl->sync_standbys_defined is not
	 * set.  See SyncRepUpdateSyncStandbysDefined.
	 *
	 * Also check that the standby hasn't already replied. Unlikely race
	 * condition but we'll be fetching that cache line anyway so it's likely
	 * to be a low cost check.
	 */
	if (((!IS_QUERY_DISPATCHER()) && !WalSndCtl->sync_standbys_defined) ||
		lsn <= WalSndCtl->lsn[mode])
	{
		elogif(debug_walrepl_syncrep, LOG,
				"syncrep wait -- Not waiting for syncrep because xlog upto LSN (%X/%X) which is "
				"greater than this backend's commit LSN (%X/%X) has already "
				"been replicated.",
			   (uint32) (WalSndCtl->lsn[mode] >> 32), (uint32) WalSndCtl->lsn[mode],
			   (uint32) (lsn >> 32), (uint32) lsn);

		LWLockRelease(SyncRepLock);
		return;
	}

	/*
	 * Set our waitLSN so WALSender will know when to wake us, and add
	 * ourselves to the queue.
	 */
	MyProc->waitLSN = lsn;
	MyProc->syncRepState = SYNC_REP_WAITING;
	SyncRepQueueInsert(mode);
	Assert(SyncRepQueueIsOrderedByLSN(mode));
	LWLockRelease(SyncRepLock);

	elogif(debug_walrepl_syncrep, LOG,
			"syncrep wait -- This backend is now inserted in the syncrep queue.");

	/* Alter ps display to show waiting for sync rep. */
	if (update_process_title)
	{
		int			len;

		old_status = get_real_act_ps_display(&len);
		/*
		 * The 32 represents the bytes in the string " waiting for %X/%X", as
		 * in upstream.  The 12 represents GPDB specific " replication" suffix.
		 */
		new_status = (char *) palloc(len + 32 + 12 + 1);
		memcpy(new_status, old_status, len);
		sprintf(new_status + len, " waiting for %X/%X replication",
				(uint32) (lsn >> 32), (uint32) lsn);
		set_ps_display(new_status, false);
		new_status[len] = '\0'; /* truncate off " waiting ..." */
	}

	/* Report the wait */
	pgstat_report_wait_start(PG_WAIT_REPLICATION);

	/*
	 * Wait for specified LSN to be confirmed.
	 *
	 * Each proc has its own wait latch, so we perform a normal latch
	 * check/wait loop here.
	 */
	for (;;)
	{
		int			rc;

		/* Must reset the latch before testing state. */
		ResetLatch(MyLatch);

		/*
		 * Acquiring the lock is not needed, the latch ensures proper
		 * barriers. If it looks like we're done, we must really be done,
		 * because once walsender changes the state to SYNC_REP_WAIT_COMPLETE,
		 * it will never update it again, so we can't be seeing a stale value
		 * in that case.
		 */
		if (MyProc->syncRepState == SYNC_REP_WAIT_COMPLETE)
		{
			elogif(debug_walrepl_syncrep, LOG,
					"syncrep wait -- This backend's syncrep state is now 'wait complete'.");
			break;
		}

		SIMPLE_FAULT_INJECTOR("sync_rep_query_die");

		/*
		 * If a wait for synchronous replication is pending, we can neither
		 * acknowledge the commit nor raise ERROR or FATAL.  The latter would
		 * lead the client to believe that the transaction aborted, which is
		 * not true: it's already committed locally. The former is no good
		 * either: the client has requested synchronous replication, and is
		 * entitled to assume that an acknowledged commit is also replicated,
		 * which might not be true. So in this case we issue a WARNING (which
		 * some clients may be able to interpret) and shut off further output.
		 * We do NOT reset ProcDiePending, so that the process will die after
		 * the commit is cleaned up.
		 */
		if (ProcDiePending)
		{
			/*
			 * For QE we should have done FATAL here so that 2PC can retry, but
			 * FATAL here makes some shm exit callback functions panic or
			 * assert fail because the transaction is still not finished, so
			 * let's defer the quitting to exec_mpp_dtx_protocol_command().
			 */
			ereport(WARNING,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("canceling the wait for synchronous replication and terminating connection due to administrator command")));
			whereToSendOutput = DestNone;
			SyncRepCancelWait();
			break;
		}

		/*
		 * GPDB: There are multiple code paths going through this function,
		 * e.g. prepare, commit, and abort. To ensure MPP cluster consistency,
		 * if primary already changed, then this backend has to wait for the
		 * xlog record replicate to the mirror to avoid inconsistency between
		 * the primary and the mirror, since they are under synced replication.
		 *
		 * If the mirror is indeed offline and prevents xlog to be synced, FTS
		 * will detect the mirror goes down, and failure handling will kick-in
		 * and mark the mirror down and out-of-sync with the primary to prevent
		 * failover. Then the syncrep will be turned off by the FTS to unblock
		 * backends waiting here.
		 */
		if (QueryCancelPending && commit)
		{
			QueryCancelPending = false;
			ereport(WARNING,
					(errmsg("ignoring query cancel request for synchronous replication to ensure cluster consistency"),
					 errdetail("The transaction has already changed locally, it has to be replicated to standby.")));
			SIMPLE_FAULT_INJECTOR("sync_rep_query_cancel");
		}

		elogif(debug_walrepl_syncrep, LOG,
				"syncrep wait -- This backend's syncrep state is still 'waiting'. "
				"Hence it will wait on a latch until awakend.")
		/*
		 * Wait on latch.  Any condition that should wake us up will set the
		 * latch, so no need for timeout.
		 */
		rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_POSTMASTER_DEATH, -1,
					   WAIT_EVENT_SYNC_REP);

		/*
		 * If the postmaster dies, we'll probably never get an acknowledgment,
		 * because all the wal sender processes will exit. So just bail out.
		 */
		if (rc & WL_POSTMASTER_DEATH)
		{
			ProcDiePending = true;
			whereToSendOutput = DestNone;
			SyncRepCancelWait();
			break;
		}
	}

	/*
	 * WalSender has checked our LSN and has removed us from queue. Clean up
	 * state and leave.  It's OK to reset these shared memory fields without
	 * holding SyncRepLock, because any walsenders will ignore us anyway when
	 * we're not on the queue.  We need a read barrier to make sure we see the
	 * changes to the queue link (this might be unnecessary without
	 * assertions, but better safe than sorry).
	 */
	pg_read_barrier();
	Assert(SHMQueueIsDetached(&(MyProc->syncRepLinks)));
	MyProc->syncRepState = SYNC_REP_NOT_WAITING;
	MyProc->waitLSN = 0;

	pgstat_report_wait_end();

	if (new_status)
	{
		/* Reset ps display */
		set_ps_display(new_status, false);
		pfree(new_status);
	}
}

/*
 * Insert MyProc into the specified SyncRepQueue, maintaining sorted invariant.
 *
 * Usually we will go at tail of queue, though it's possible that we arrive
 * here out of order, so start at tail and work back to insertion point.
 */
static void
SyncRepQueueInsert(int mode)
{
	PGPROC	   *proc;

	Assert(mode >= 0 && mode < NUM_SYNC_REP_WAIT_MODE);
	proc = (PGPROC *) SHMQueuePrev(&(WalSndCtl->SyncRepQueue[mode]),
								   &(WalSndCtl->SyncRepQueue[mode]),
								   offsetof(PGPROC, syncRepLinks));

	while (proc)
	{
		/*
		 * Stop at the queue element that we should after to ensure the queue
		 * is ordered by LSN.
		 */
		if (proc->waitLSN < MyProc->waitLSN)
			break;

		proc = (PGPROC *) SHMQueuePrev(&(WalSndCtl->SyncRepQueue[mode]),
									   &(proc->syncRepLinks),
									   offsetof(PGPROC, syncRepLinks));
	}

	if (proc)
		SHMQueueInsertAfter(&(proc->syncRepLinks), &(MyProc->syncRepLinks));
	else
		SHMQueueInsertAfter(&(WalSndCtl->SyncRepQueue[mode]), &(MyProc->syncRepLinks));
}

/*
 * Acquire SyncRepLock and cancel any wait currently in progress.
 */
static void
SyncRepCancelWait(void)
{
	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
	if (!SHMQueueIsDetached(&(MyProc->syncRepLinks)))
		SHMQueueDelete(&(MyProc->syncRepLinks));
	MyProc->syncRepState = SYNC_REP_NOT_WAITING;
	LWLockRelease(SyncRepLock);
}

void
SyncRepCleanupAtProcExit(void)
{
	if (!SHMQueueIsDetached(&(MyProc->syncRepLinks)))
	{
		LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
		SHMQueueDelete(&(MyProc->syncRepLinks));
		LWLockRelease(SyncRepLock);
	}
}

/*
 * ===========================================================
 * Synchronous Replication functions for wal sender processes
 * ===========================================================
 */

/*
 * Take any action required to initialise sync rep state from config
 * data. Called at WALSender startup and after each SIGHUP.
 */
void
SyncRepInitConfig(void)
{
	int			priority;

	/*
	 * Determine if we are a potential sync standby and remember the result
	 * for handling replies from standby.
	 */
	priority = SyncRepGetStandbyPriority();
	if (MyWalSnd->sync_standby_priority != priority)
	{
		LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
		MyWalSnd->sync_standby_priority = priority;
		LWLockRelease(SyncRepLock);
		ereport(DEBUG1,
				(errmsg("standby \"%s\" now has synchronous standby priority %u",
						application_name, priority)));
	}
}

/*
 * Update the LSNs on each queue based upon our latest state. This
 * implements a simple policy of first-valid-sync-standby-releases-waiter.
 *
 * Other policies are possible, which would change what we do here and
 * perhaps also which information we store as well.
 */
void
SyncRepReleaseWaiters(void)
{
	volatile WalSndCtlData *walsndctl = WalSndCtl;
	XLogRecPtr	writePtr;
	XLogRecPtr	flushPtr;
	XLogRecPtr	applyPtr;
	bool		got_recptr;
	bool		am_sync;
	int			numwrite = 0;
	int			numflush = 0;
	int			numapply = 0;

	/*
	 * If this WALSender is serving a standby that is not on the list of
	 * potential sync standbys then we have nothing to do. If we are still
	 * starting up, still running base backup or the current flush position is
	 * still invalid, then leave quickly also.  Streaming or stopping WAL
	 * senders are allowed to release waiters.
	 */
	if (MyWalSnd->sync_standby_priority == 0 ||
		(MyWalSnd->state != WALSNDSTATE_STREAMING &&
		 MyWalSnd->state != WALSNDSTATE_STOPPING) ||
		XLogRecPtrIsInvalid(MyWalSnd->flush))
	{
		announce_next_takeover = true;
		return;
	}

	/*
	 * We're a potential sync standby. Release waiters if there are enough
	 * sync standbys and we are considered as sync.
	 */
	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);

	/*
	 * Check whether we are a sync standby or not, and calculate the synced
	 * positions among all sync standbys.
	 */
	got_recptr = SyncRepGetSyncRecPtr(&writePtr, &flushPtr, &applyPtr, &am_sync);

	/*
	 * If we are managing a sync standby, though we weren't prior to this,
	 * then announce we are now a sync standby.
	 */
	if (announce_next_takeover && am_sync)
	{
		announce_next_takeover = false;

		if (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY)
			ereport(LOG,
					(errmsg("standby \"%s\" is now a synchronous standby with priority %u",
							application_name, MyWalSnd->sync_standby_priority)));
		else
			ereport(LOG,
					(errmsg("standby \"%s\" is now a candidate for quorum synchronous standby",
							application_name)));
	}

	/*
	 * If the number of sync standbys is less than requested or we aren't
	 * managing a sync standby then just leave.
	 */
	if (!got_recptr || !am_sync)
	{
		LWLockRelease(SyncRepLock);
		announce_next_takeover = !am_sync;
		return;
	}

	/*
	 * Set the lsn first so that when we wake backends they will release up to
	 * this location.
	 */
	if (walsndctl->lsn[SYNC_REP_WAIT_WRITE] < writePtr)
	{
		walsndctl->lsn[SYNC_REP_WAIT_WRITE] = writePtr;
		numwrite = SyncRepWakeQueue(false, SYNC_REP_WAIT_WRITE);
	}
	if (walsndctl->lsn[SYNC_REP_WAIT_FLUSH] < flushPtr)
	{
		walsndctl->lsn[SYNC_REP_WAIT_FLUSH] = flushPtr;
		numflush = SyncRepWakeQueue(false, SYNC_REP_WAIT_FLUSH);
	}
	if (walsndctl->lsn[SYNC_REP_WAIT_APPLY] < applyPtr)
	{
		walsndctl->lsn[SYNC_REP_WAIT_APPLY] = applyPtr;
		numapply = SyncRepWakeQueue(false, SYNC_REP_WAIT_APPLY);
	}

	LWLockRelease(SyncRepLock);

	elogif(debug_walrepl_syncrep, LOG,
		 "released %d procs up to write %X/%X, %d procs up to flush %X/%X, %d procs up to apply %X/%X",
		 numwrite, (uint32) (writePtr >> 32), (uint32) writePtr,
		 numflush, (uint32) (flushPtr >> 32), (uint32) flushPtr,
		 numapply, (uint32) (applyPtr >> 32), (uint32) applyPtr);
}

/*
 * Calculate the synced Write, Flush and Apply positions among sync standbys.
 *
 * Return false if the number of sync standbys is less than
 * synchronous_standby_names specifies. Otherwise return true and
 * store the positions into *writePtr, *flushPtr and *applyPtr.
 *
 * On return, *am_sync is set to true if this walsender is connecting to
 * sync standby. Otherwise it's set to false.
 */
static bool
SyncRepGetSyncRecPtr(XLogRecPtr *writePtr, XLogRecPtr *flushPtr,
					 XLogRecPtr *applyPtr, bool *am_sync)
{
	List	   *sync_standbys;

	*writePtr = InvalidXLogRecPtr;
	*flushPtr = InvalidXLogRecPtr;
	*applyPtr = InvalidXLogRecPtr;
	*am_sync = false;

	/* Get standbys that are considered as synchronous at this moment */
	sync_standbys = SyncRepGetSyncStandbys(am_sync);

	/*
	 * Quick exit if we are not managing a sync standby or there are not
	 * enough synchronous standbys.
	 */
	if (IS_QUERY_DISPATCHER())
	{
		if (list_length(sync_standbys) == 0)
			return false;
	}
	else if (!(*am_sync) ||
		SyncRepConfig == NULL ||
		list_length(sync_standbys) < SyncRepConfig->num_sync)
	{
		list_free(sync_standbys);
		return false;
	}

	/*
	 * In a priority-based sync replication, the synced positions are the
	 * oldest ones among sync standbys. In a quorum-based, they are the Nth
	 * latest ones.
	 *
	 * SyncRepGetNthLatestSyncRecPtr() also can calculate the oldest
	 * positions. But we use SyncRepGetOldestSyncRecPtr() for that calculation
	 * because it's a bit more efficient.
	 *
	 * XXX If the numbers of current and requested sync standbys are the same,
	 * we can use SyncRepGetOldestSyncRecPtr() to calculate the synced
	 * positions even in a quorum-based sync replication.
	 */
	if (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY)
	{
		SyncRepGetOldestSyncRecPtr(writePtr, flushPtr, applyPtr,
								   sync_standbys);
	}
	else
	{
		SyncRepGetNthLatestSyncRecPtr(writePtr, flushPtr, applyPtr,
									  sync_standbys, SyncRepConfig->num_sync);
	}

	list_free(sync_standbys);
	return true;
}

/*
 * Calculate the oldest Write, Flush and Apply positions among sync standbys.
 */
static void
SyncRepGetOldestSyncRecPtr(XLogRecPtr *writePtr, XLogRecPtr *flushPtr,
						   XLogRecPtr *applyPtr, List *sync_standbys)
{
	ListCell   *cell;

	/*
	 * Scan through all sync standbys and calculate the oldest Write, Flush
	 * and Apply positions.
	 */
	foreach(cell, sync_standbys)
	{
		WalSnd	   *walsnd = &WalSndCtl->walsnds[lfirst_int(cell)];
		XLogRecPtr	write;
		XLogRecPtr	flush;
		XLogRecPtr	apply;

		SpinLockAcquire(&walsnd->mutex);
		write = walsnd->write;
		flush = walsnd->flush;
		apply = walsnd->apply;
		SpinLockRelease(&walsnd->mutex);

		if (XLogRecPtrIsInvalid(*writePtr) || *writePtr > write)
			*writePtr = write;
		if (XLogRecPtrIsInvalid(*flushPtr) || *flushPtr > flush)
			*flushPtr = flush;
		if (XLogRecPtrIsInvalid(*applyPtr) || *applyPtr > apply)
			*applyPtr = apply;
	}
}

/*
 * Calculate the Nth latest Write, Flush and Apply positions among sync
 * standbys.
 */
static void
SyncRepGetNthLatestSyncRecPtr(XLogRecPtr *writePtr, XLogRecPtr *flushPtr,
							  XLogRecPtr *applyPtr, List *sync_standbys, uint8 nth)
{
	ListCell   *cell;
	XLogRecPtr *write_array;
	XLogRecPtr *flush_array;
	XLogRecPtr *apply_array;
	int			len;
	int			i = 0;

	len = list_length(sync_standbys);
	write_array = (XLogRecPtr *) palloc(sizeof(XLogRecPtr) * len);
	flush_array = (XLogRecPtr *) palloc(sizeof(XLogRecPtr) * len);
	apply_array = (XLogRecPtr *) palloc(sizeof(XLogRecPtr) * len);

	foreach(cell, sync_standbys)
	{
		WalSnd	   *walsnd = &WalSndCtl->walsnds[lfirst_int(cell)];

		SpinLockAcquire(&walsnd->mutex);
		write_array[i] = walsnd->write;
		flush_array[i] = walsnd->flush;
		apply_array[i] = walsnd->apply;
		SpinLockRelease(&walsnd->mutex);

		i++;
	}

	/* Sort each array in descending order */
	qsort(write_array, len, sizeof(XLogRecPtr), cmp_lsn);
	qsort(flush_array, len, sizeof(XLogRecPtr), cmp_lsn);
	qsort(apply_array, len, sizeof(XLogRecPtr), cmp_lsn);

	/* Get Nth latest Write, Flush, Apply positions */
	*writePtr = write_array[nth - 1];
	*flushPtr = flush_array[nth - 1];
	*applyPtr = apply_array[nth - 1];

	pfree(write_array);
	pfree(flush_array);
	pfree(apply_array);
}

/*
 * Compare lsn in order to sort array in descending order.
 */
static int
cmp_lsn(const void *a, const void *b)
{
	XLogRecPtr	lsn1 = *((const XLogRecPtr *) a);
	XLogRecPtr	lsn2 = *((const XLogRecPtr *) b);

	if (lsn1 > lsn2)
		return -1;
	else if (lsn1 == lsn2)
		return 0;
	else
		return 1;
}

/*
 * Return the list of sync standbys, or NIL if no sync standby is connected.
 *
 * The caller must hold SyncRepLock.
 *
 * On return, *am_sync is set to true if this walsender is connecting to
 * sync standby. Otherwise it's set to false.
 */
List *
SyncRepGetSyncStandbys(bool *am_sync)
{
	List	   *result = NIL;
	bool		syncStandbyPresent;
	int			i;
	volatile WalSnd *walsnd;	/* Use volatile pointer to prevent code
								 * rearrangement */

	/* Set default result */
	if (am_sync != NULL)
		*am_sync = false;

	/* GPDB_12_MERGE_FIXME: Should this be in SyncRepGetSyncStandbysQuorum()
	 * instead? */
	if (IS_QUERY_DISPATCHER())
	{
		for (i = 0; i < max_wal_senders; i++)
		{
			walsnd = &WalSndCtl->walsnds[i];
			SpinLockAcquire(&walsnd->mutex);
			syncStandbyPresent = (walsnd->pid != 0)
				&& ((walsnd->state == WALSNDSTATE_STREAMING)
				|| (walsnd->state == WALSNDSTATE_CATCHUP &&
				walsnd->caughtup_within_range));
			SpinLockRelease(&walsnd->mutex);

			if (syncStandbyPresent)
			{
				result = lappend_int(result, i);
				if (am_sync)
					*am_sync = true;
				return result;
			}
		}
	}

	/* Quick exit if sync replication is not requested */
	if (SyncRepConfig == NULL)
		return NIL;

	return (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY) ?
		SyncRepGetSyncStandbysPriority(am_sync) :
		SyncRepGetSyncStandbysQuorum(am_sync);
}

/*
 * Return the list of all the candidates for quorum sync standbys,
 * or NIL if no such standby is connected.
 *
 * The caller must hold SyncRepLock. This function must be called only in
 * a quorum-based sync replication.
 *
 * On return, *am_sync is set to true if this walsender is connecting to
 * sync standby. Otherwise it's set to false.
 */
static List *
SyncRepGetSyncStandbysQuorum(bool *am_sync)
{
	List	   *result = NIL;
	int			i;
	volatile WalSnd *walsnd;	/* Use volatile pointer to prevent code
								 * rearrangement */

	Assert(SyncRepConfig->syncrep_method == SYNC_REP_QUORUM);

	for (i = 0; i < max_wal_senders; i++)
	{
		XLogRecPtr	flush;
		WalSndState state;
		int			pid;

		walsnd = &WalSndCtl->walsnds[i];

		SpinLockAcquire(&walsnd->mutex);
		pid = walsnd->pid;
		flush = walsnd->flush;
		state = walsnd->state;
		SpinLockRelease(&walsnd->mutex);

		/* Must be active */
		if (pid == 0)
			continue;

		/* Must be streaming or stopping */
		if (state != WALSNDSTATE_STREAMING &&
			state != WALSNDSTATE_STOPPING)
			continue;

		/* Must be synchronous */
		if (walsnd->sync_standby_priority == 0)
			continue;

		/* Must have a valid flush position */
		if (XLogRecPtrIsInvalid(flush))
			continue;

		/*
		 * Consider this standby as a candidate for quorum sync standbys and
		 * append it to the result.
		 */
		result = lappend_int(result, i);
		if (am_sync != NULL && walsnd == MyWalSnd)
			*am_sync = true;
	}

	return result;
}

/*
 * Return the list of sync standbys chosen based on their priorities,
 * or NIL if no sync standby is connected.
 *
 * If there are multiple standbys with the same priority,
 * the first one found is selected preferentially.
 *
 * The caller must hold SyncRepLock. This function must be called only in
 * a priority-based sync replication.
 *
 * On return, *am_sync is set to true if this walsender is connecting to
 * sync standby. Otherwise it's set to false.
 */
static List *
SyncRepGetSyncStandbysPriority(bool *am_sync)
{
	List	   *result = NIL;
	List	   *pending = NIL;
	int			lowest_priority;
	int			next_highest_priority;
	int			this_priority;
	int			priority;
	int			i;
	bool		am_in_pending = false;
	volatile WalSnd *walsnd;	/* Use volatile pointer to prevent code
								 * rearrangement */

	Assert(SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY);

	lowest_priority = SyncRepConfig->nmembers;
	next_highest_priority = lowest_priority + 1;

	/*
	 * Find the sync standbys which have the highest priority (i.e, 1). Also
	 * store all the other potential sync standbys into the pending list, in
	 * order to scan it later and find other sync standbys from it quickly.
	 */
	for (i = 0; i < max_wal_senders; i++)
	{
		XLogRecPtr	flush;
		WalSndState state;
		int			pid;

		walsnd = &WalSndCtl->walsnds[i];

		SpinLockAcquire(&walsnd->mutex);
		pid = walsnd->pid;
		flush = walsnd->flush;
		state = walsnd->state;
		SpinLockRelease(&walsnd->mutex);

		/* Must be active */
		if (pid == 0)
			continue;

		/* Must be streaming or stopping */
		if (state != WALSNDSTATE_STREAMING &&
			state != WALSNDSTATE_STOPPING)
			continue;

		/* Must be synchronous */
		this_priority = walsnd->sync_standby_priority;
		if (this_priority == 0)
			continue;

		/* Must have a valid flush position */
		if (XLogRecPtrIsInvalid(flush))
			continue;

		/*
		 * If the priority is equal to 1, consider this standby as sync and
		 * append it to the result. Otherwise append this standby to the
		 * pending list to check if it's actually sync or not later.
		 */
		if (this_priority == 1)
		{
			result = lappend_int(result, i);
			if (am_sync != NULL && walsnd == MyWalSnd)
				*am_sync = true;
			if (list_length(result) == SyncRepConfig->num_sync)
			{
				list_free(pending);
				return result;	/* Exit if got enough sync standbys */
			}
		}
		else
		{
			pending = lappend_int(pending, i);
			if (am_sync != NULL && walsnd == MyWalSnd)
				am_in_pending = true;

			/*
			 * Track the highest priority among the standbys in the pending
			 * list, in order to use it as the starting priority for later
			 * scan of the list. This is useful to find quickly the sync
			 * standbys from the pending list later because we can skip
			 * unnecessary scans for the unused priorities.
			 */
			if (this_priority < next_highest_priority)
				next_highest_priority = this_priority;
		}
	}

	/*
	 * Consider all pending standbys as sync if the number of them plus
	 * already-found sync ones is lower than the configuration requests.
	 */
	if (list_length(result) + list_length(pending) <= SyncRepConfig->num_sync)
	{
		bool		needfree = (result != NIL && pending != NIL);

		/*
		 * Set *am_sync to true if this walsender is in the pending list
		 * because all pending standbys are considered as sync.
		 */
		if (am_sync != NULL && !(*am_sync))
			*am_sync = am_in_pending;

		result = list_concat(result, pending);
		if (needfree)
			pfree(pending);
		return result;
	}

	/*
	 * Find the sync standbys from the pending list.
	 */
	priority = next_highest_priority;
	while (priority <= lowest_priority)
	{
		ListCell   *cell;
		ListCell   *prev = NULL;
		ListCell   *next;

		next_highest_priority = lowest_priority + 1;

		for (cell = list_head(pending); cell != NULL; cell = next)
		{
			i = lfirst_int(cell);
			walsnd = &WalSndCtl->walsnds[i];

			next = lnext(cell);

			this_priority = walsnd->sync_standby_priority;
			if (this_priority == priority)
			{
				result = lappend_int(result, i);
				if (am_sync != NULL && walsnd == MyWalSnd)
					*am_sync = true;

				/*
				 * We should always exit here after the scan of pending list
				 * starts because we know that the list has enough elements to
				 * reach SyncRepConfig->num_sync.
				 */
				if (list_length(result) == SyncRepConfig->num_sync)
				{
					list_free(pending);
					return result;	/* Exit if got enough sync standbys */
				}

				/*
				 * Remove the entry for this sync standby from the list to
				 * prevent us from looking at the same entry again.
				 */
				pending = list_delete_cell(pending, cell, prev);

				continue;
			}

			if (this_priority < next_highest_priority)
				next_highest_priority = this_priority;

			prev = cell;
		}

		priority = next_highest_priority;
	}

	/* never reached, but keep compiler quiet */
	Assert(false);
	return result;
}

/*
 * Check if we are in the list of sync standbys, and if so, determine
 * priority sequence. Return priority if set, or zero to indicate that
 * we are not a potential sync standby.
 *
 * **Note:- Currently, GPDB does NOT apply the concept of standby priority as
 *   we allow max 1 walsender to be alive at a time. Hence, this function returns
 *   1.**
 */
static int
SyncRepGetStandbyPriority(void)
{
	/* Currently set the priority to 1 */
	return 1;
}

/*
 * Walk the specified queue from head.  Set the state of any backends that
 * need to be woken, remove them from the queue, and then wake them.
 * Pass all = true to wake whole queue; otherwise, just wake up to
 * the walsender's LSN.
 *
 * Must hold SyncRepLock.
 */
int
SyncRepWakeQueue(bool all, int mode)
{
	volatile WalSndCtlData *walsndctl = WalSndCtl;
	PGPROC	   *proc = NULL;
	PGPROC	   *thisproc = NULL;
	int			numprocs = 0;

	Assert(mode >= 0 && mode < NUM_SYNC_REP_WAIT_MODE);
	Assert(SyncRepQueueIsOrderedByLSN(mode));

	proc = (PGPROC *) SHMQueueNext(&(WalSndCtl->SyncRepQueue[mode]),
								   &(WalSndCtl->SyncRepQueue[mode]),
								   offsetof(PGPROC, syncRepLinks));

	while (proc)
	{
		/*
		 * Assume the queue is ordered by LSN
		 */
		if (!all && walsndctl->lsn[mode] < proc->waitLSN)
			return numprocs;

		/*
		 * Move to next proc, so we can delete thisproc from the queue.
		 * thisproc is valid, proc may be NULL after this.
		 */
		thisproc = proc;
		proc = (PGPROC *) SHMQueueNext(&(WalSndCtl->SyncRepQueue[mode]),
									   &(proc->syncRepLinks),
									   offsetof(PGPROC, syncRepLinks));

		/*
		 * Remove thisproc from queue.
		 */
		SHMQueueDelete(&(thisproc->syncRepLinks));

		/*
		 * SyncRepWaitForLSN() reads syncRepState without holding the lock, so
		 * make sure that it sees the queue link being removed before the
		 * syncRepState change.
		 */
		pg_write_barrier();

		/*
		 * Set state to complete; see SyncRepWaitForLSN() for discussion of
		 * the various states.
		 */
		thisproc->syncRepState = SYNC_REP_WAIT_COMPLETE;

		/*
		 * Wake only when we have set state and removed from queue.
		 */
		SetLatch(&(thisproc->procLatch));

		elogif(debug_walrepl_syncrep, LOG,
				"syncrep wakeup queue -- %d procid was removed from syncrep queue. "
				"Its state is changed to 'Wait Complete' and "
				"its latch is now set",
				thisproc->pid);

		numprocs++;
	}

	return numprocs;
}

/*
 * The checkpointer calls this as needed to update the shared
 * sync_standbys_defined flag, so that backends don't remain permanently wedged
 * if synchronous_standby_names is unset.  It's safe to check the current value
 * without the lock, because it's only ever updated by one process.  But we
 * must take the lock to change it.
 */
void
SyncRepUpdateSyncStandbysDefined(void)
{
	bool		sync_standbys_defined = SyncStandbysDefined();

	if (sync_standbys_defined != WalSndCtl->sync_standbys_defined)
	{
		LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);

		/*
		 * If synchronous_standby_names has been reset to empty, it's futile
		 * for backends to continue to waiting.  Since the user no longer
		 * wants synchronous replication, we'd better wake them up.
		 */
		if (!sync_standbys_defined)
		{
			int			i;

			for (i = 0; i < NUM_SYNC_REP_WAIT_MODE; i++)
				SyncRepWakeQueue(true, i);
		}

		/*
		 * Only allow people to join the queue when there are synchronous
		 * standbys defined.  Without this interlock, there's a race
		 * condition: we might wake up all the current waiters; then, some
		 * backend that hasn't yet reloaded its config might go to sleep on
		 * the queue (and never wake up).  This prevents that.
		 */
		WalSndCtl->sync_standbys_defined = sync_standbys_defined;

		LWLockRelease(SyncRepLock);
	}
}

#ifdef USE_ASSERT_CHECKING
static bool
SyncRepQueueIsOrderedByLSN(int mode)
{
	PGPROC	   *proc = NULL;
	XLogRecPtr	lastLSN;

	Assert(mode >= 0 && mode < NUM_SYNC_REP_WAIT_MODE);

	lastLSN = 0;

	proc = (PGPROC *) SHMQueueNext(&(WalSndCtl->SyncRepQueue[mode]),
								   &(WalSndCtl->SyncRepQueue[mode]),
								   offsetof(PGPROC, syncRepLinks));

	while (proc)
	{
		/*
		 * Check the queue is ordered by LSN.
		 *
		 * In upstream this check also validates that multiple procs don't
		 * have matching LSNs. This restriction is lifted in GPDB as for
		 * commit-prepared retry case since we don't know the exact lsn of
		 * commit-prepared record, need to wait for latest flush point
		 * lsn. So, its possible due to concurrency multiple backends register
		 * in queue with same lsn value. The check here anyways seems little
		 * restrictive as actual queue usage only needs it in sorted order and
		 * not really relies on having unique entries. It just happens to be
		 * that if all usage of SyncRepWaitForLSN() feed unique lsn value
		 * upstream and in GPDB except from FinishPreparedTransaction(), but
		 * not required for correct functioning of the code.
		 */
		if (proc->waitLSN < lastLSN)
			return false;

		lastLSN = proc->waitLSN;

		proc = (PGPROC *) SHMQueueNext(&(WalSndCtl->SyncRepQueue[mode]),
									   &(proc->syncRepLinks),
									   offsetof(PGPROC, syncRepLinks));
	}

	return true;
}
#endif

/*
 * ===========================================================
 * Synchronous Replication functions executed by any process
 * ===========================================================
 */

bool
check_synchronous_standby_names(char **newval, void **extra, GucSource source)
{
	if (*newval != NULL && (*newval)[0] != '\0')
	{
		int			parse_rc;
		SyncRepConfigData *pconf;

		/* Reset communication variables to ensure a fresh start */
		syncrep_parse_result = NULL;
		syncrep_parse_error_msg = NULL;

		/* Parse the synchronous_standby_names string */
		syncrep_scanner_init(*newval);
		parse_rc = syncrep_yyparse();
		syncrep_scanner_finish();

		if (parse_rc != 0 || syncrep_parse_result == NULL)
		{
			GUC_check_errcode(ERRCODE_SYNTAX_ERROR);
			if (syncrep_parse_error_msg)
				GUC_check_errdetail("%s", syncrep_parse_error_msg);
			else
				GUC_check_errdetail("synchronous_standby_names parser failed");
			return false;
		}

		if (syncrep_parse_result->num_sync <= 0)
		{
			GUC_check_errmsg("number of synchronous standbys (%d) must be greater than zero",
							 syncrep_parse_result->num_sync);
			return false;
		}

		/* GUC extra value must be malloc'd, not palloc'd */
		pconf = (SyncRepConfigData *)
			malloc(syncrep_parse_result->config_size);
		if (pconf == NULL)
			return false;
		memcpy(pconf, syncrep_parse_result, syncrep_parse_result->config_size);

		*extra = (void *) pconf;

		/*
		 * We need not explicitly clean up syncrep_parse_result.  It, and any
		 * other cruft generated during parsing, will be freed when the
		 * current memory context is deleted.  (This code is generally run in
		 * a short-lived context used for config file processing, so that will
		 * not be very long.)
		 */
	}
	else
		*extra = NULL;

	return true;
}

void
assign_synchronous_standby_names(const char *newval, void *extra)
{
	SyncRepConfig = (SyncRepConfigData *) extra;
}

void
assign_synchronous_commit(int newval, void *extra)
{
	switch (newval)
	{
		case SYNCHRONOUS_COMMIT_REMOTE_WRITE:
			SyncRepWaitMode = SYNC_REP_WAIT_WRITE;
			break;
		case SYNCHRONOUS_COMMIT_REMOTE_FLUSH:
			SyncRepWaitMode = SYNC_REP_WAIT_FLUSH;
			break;
		case SYNCHRONOUS_COMMIT_REMOTE_APPLY:
			SyncRepWaitMode = SYNC_REP_WAIT_APPLY;
			break;
		default:
			SyncRepWaitMode = SYNC_REP_NO_WAIT;
			break;
	}
}
