/*-------------------------------------------------------------------------
 * output_plugin.h
 *	   PostgreSQL Logical Decode Plugin Interface
 *
 * Copyright (c) 2012-2019, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#ifndef OUTPUT_PLUGIN_H
#define OUTPUT_PLUGIN_H

#include "replication/reorderbuffer.h"

struct LogicalDecodingContext;
struct OutputPluginCallbacks;

typedef enum OutputPluginOutputType
{
	OUTPUT_PLUGIN_BINARY_OUTPUT,
	OUTPUT_PLUGIN_TEXTUAL_OUTPUT
} OutputPluginOutputType;

/*
 * Options set by the output plugin, in the startup callback.
 */
typedef struct OutputPluginOptions
{
	OutputPluginOutputType output_type;
	bool		receive_rewrites;
} OutputPluginOptions;

/*
 * Type of the shared library symbol _PG_output_plugin_init that is looked up
 * when loading an output plugin shared library.
 */
typedef void (*LogicalOutputPluginInit) (struct OutputPluginCallbacks *cb);

/*
 * Callback that gets called in a user-defined plugin. ctx->private_data can
 * be set to some private data.
 *
 * "is_init" will be set to "true" if the decoding slot just got defined. When
 * the same slot is used from there one, it will be "false".
 */
typedef void (*LogicalDecodeStartupCB) (struct LogicalDecodingContext *ctx,
<<<<<<< HEAD
												OutputPluginOptions *options,
													bool is_init);
=======
										OutputPluginOptions *options,
										bool is_init);
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196

/*
 * Callback called for every (explicit or implicit) BEGIN of a successful
 * transaction.
 */
typedef void (*LogicalDecodeBeginCB) (struct LogicalDecodingContext *ctx,
<<<<<<< HEAD
												  ReorderBufferTXN *txn);
=======
									  ReorderBufferTXN *txn);
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196

/*
 * Callback for every individual change in a successful transaction.
 */
typedef void (*LogicalDecodeChangeCB) (struct LogicalDecodingContext *ctx,
<<<<<<< HEAD
												   ReorderBufferTXN *txn,
												   Relation relation,
												ReorderBufferChange *change);
=======
									   ReorderBufferTXN *txn,
									   Relation relation,
									   ReorderBufferChange *change);

/*
 * Callback for every TRUNCATE in a successful transaction.
 */
typedef void (*LogicalDecodeTruncateCB) (struct LogicalDecodingContext *ctx,
										 ReorderBufferTXN *txn,
										 int nrelations,
										 Relation relations[],
										 ReorderBufferChange *change);
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196

/*
 * Called for every (explicit or implicit) COMMIT of a successful transaction.
 */
typedef void (*LogicalDecodeCommitCB) (struct LogicalDecodingContext *ctx,
<<<<<<< HEAD
												   ReorderBufferTXN *txn,
												   XLogRecPtr commit_lsn);
=======
									   ReorderBufferTXN *txn,
									   XLogRecPtr commit_lsn);
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196

/*
 * Called for the generic logical decoding messages.
 */
typedef void (*LogicalDecodeMessageCB) (struct LogicalDecodingContext *ctx,
										ReorderBufferTXN *txn,
										XLogRecPtr message_lsn,
										bool transactional,
										const char *prefix,
										Size message_size,
										const char *message);

/*
 * Filter changes by origin.
 */
typedef bool (*LogicalDecodeFilterByOriginCB) (struct LogicalDecodingContext *ctx,
											   RepOriginId origin_id);

/*
 * Called to shutdown an output plugin.
 */
typedef void (*LogicalDecodeShutdownCB) (struct LogicalDecodingContext *ctx);

/*
 * Output plugin callbacks
 */
typedef struct OutputPluginCallbacks
{
	LogicalDecodeStartupCB startup_cb;
	LogicalDecodeBeginCB begin_cb;
	LogicalDecodeChangeCB change_cb;
	LogicalDecodeTruncateCB truncate_cb;
	LogicalDecodeCommitCB commit_cb;
	LogicalDecodeMessageCB message_cb;
	LogicalDecodeFilterByOriginCB filter_by_origin_cb;
	LogicalDecodeShutdownCB shutdown_cb;
} OutputPluginCallbacks;

/* Functions in replication/logical/logical.c */
extern void OutputPluginPrepareWrite(struct LogicalDecodingContext *ctx, bool last_write);
extern void OutputPluginWrite(struct LogicalDecodingContext *ctx, bool last_write);
<<<<<<< HEAD
=======
extern void OutputPluginUpdateProgress(struct LogicalDecodingContext *ctx);
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196

#endif							/* OUTPUT_PLUGIN_H */
