/*-------------------------------------------------------------------------
 *
 * nodeHash.h
 *	  prototypes for nodeHash.c
 *
 *
<<<<<<< HEAD
 * Portions Copyright (c) 2007-2008, Greenplum inc
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
=======
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeHash.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEHASH_H
#define NODEHASH_H

#include "access/parallel.h"
#include "nodes/execnodes.h"
#include "executor/hashjoin.h"  /* for HJTUPLE_OVERHEAD */
#include "access/memtup.h"

struct SharedHashJoinBatch;

extern HashState *ExecInitHash(Hash *node, EState *estate, int eflags);
<<<<<<< HEAD
extern struct TupleTableSlot *ExecHash(HashState *node);
=======
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196
extern Node *MultiExecHash(HashState *node);
extern void ExecEndHash(HashState *node);
extern void ExecReScanHash(HashState *node);

<<<<<<< HEAD
extern HashJoinTable ExecHashTableCreate(HashState *hashState, HashJoinState *hjstate,
					List *hashOperators, bool keepNulls,
					uint64 operatorMemKB);
extern void ExecHashTableDestroy(HashState *hashState, HashJoinTable hashtable);
extern bool ExecHashTableInsert(HashState *hashState, HashJoinTable hashtable,
					struct TupleTableSlot *slot,
					uint32 hashvalue);
extern bool ExecHashGetHashValue(HashState *hashState, HashJoinTable hashtable,
					 ExprContext *econtext,
					 List *hashkeys,
					 bool outer_tuple,
					 bool keep_nulls,
					 uint32 *hashvalue,
					 bool *hashkeys_null);
extern void ExecHashGetBucketAndBatch(HashJoinTable hashtable,
						  uint32 hashvalue,
						  int *bucketno,
						  int *batchno);
extern bool ExecScanHashBucket(HashState *hashState, HashJoinState *hjstate,
				   ExprContext *econtext);
extern void ExecPrepHashTableForUnmatched(HashJoinState *hjstate);
extern bool ExecScanHashTableForUnmatched(HashJoinState *hjstate,
							  ExprContext *econtext);
extern void ExecHashTableReset(HashState *hashState, HashJoinTable hashtable);
extern void ExecHashTableResetMatchFlags(HashJoinTable hashtable);
extern void ExecChooseHashTableSize(double ntuples, int tupwidth, bool useskew,
						uint64 operatorMemKB,
						int *numbuckets,
						int *numbatches,
						int *num_skew_mcvs);
=======
extern HashJoinTable ExecHashTableCreate(HashState *state, List *hashOperators, List *hashCollations,
										 bool keepNulls);
extern void ExecParallelHashTableAlloc(HashJoinTable hashtable,
									   int batchno);
extern void ExecHashTableDestroy(HashJoinTable hashtable);
extern void ExecHashTableDetach(HashJoinTable hashtable);
extern void ExecHashTableDetachBatch(HashJoinTable hashtable);
extern void ExecParallelHashTableSetCurrentBatch(HashJoinTable hashtable,
												 int batchno);

extern void ExecHashTableInsert(HashJoinTable hashtable,
								TupleTableSlot *slot,
								uint32 hashvalue);
extern void ExecParallelHashTableInsert(HashJoinTable hashtable,
										TupleTableSlot *slot,
										uint32 hashvalue);
extern void ExecParallelHashTableInsertCurrentBatch(HashJoinTable hashtable,
													TupleTableSlot *slot,
													uint32 hashvalue);
extern bool ExecHashGetHashValue(HashJoinTable hashtable,
								 ExprContext *econtext,
								 List *hashkeys,
								 bool outer_tuple,
								 bool keep_nulls,
								 uint32 *hashvalue);
extern void ExecHashGetBucketAndBatch(HashJoinTable hashtable,
									  uint32 hashvalue,
									  int *bucketno,
									  int *batchno);
extern bool ExecScanHashBucket(HashJoinState *hjstate, ExprContext *econtext);
extern bool ExecParallelScanHashBucket(HashJoinState *hjstate, ExprContext *econtext);
extern void ExecPrepHashTableForUnmatched(HashJoinState *hjstate);
extern bool ExecScanHashTableForUnmatched(HashJoinState *hjstate,
										  ExprContext *econtext);
extern void ExecHashTableReset(HashJoinTable hashtable);
extern void ExecHashTableResetMatchFlags(HashJoinTable hashtable);
extern void ExecChooseHashTableSize(double ntuples, int tupwidth, bool useskew,
									bool try_combined_work_mem,
									int parallel_workers,
									size_t *space_allowed,
									int *numbuckets,
									int *numbatches,
									int *num_skew_mcvs);
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196
extern int	ExecHashGetSkewBucket(HashJoinTable hashtable, uint32 hashvalue);
extern void ExecHashEstimate(HashState *node, ParallelContext *pcxt);
extern void ExecHashInitializeDSM(HashState *node, ParallelContext *pcxt);
extern void ExecHashInitializeWorker(HashState *node, ParallelWorkerContext *pwcxt);
extern void ExecHashRetrieveInstrumentation(HashState *node);
extern void ExecShutdownHash(HashState *node);
extern void ExecHashGetInstrumentation(HashInstrumentation *instrument,
									   HashJoinTable hashtable);

<<<<<<< HEAD
extern void ExecHashTableExplainInit(HashState *hashState, HashJoinState *hjstate,
                                     HashJoinTable  hashtable);
extern void ExecHashTableExplainBatchEnd(HashState *hashState, HashJoinTable hashtable);

static inline int
ExecHashRowSize(int tupwidth)
{
    return HJTUPLE_OVERHEAD +
		MAXALIGN(sizeof(MemTupleData)) +
		MAXALIGN(tupwidth);
}                               /* ExecHashRowSize */
#endif   /* NODEHASH_H */
=======
#endif							/* NODEHASH_H */
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196
