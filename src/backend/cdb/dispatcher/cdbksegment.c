
/*
   placeholder
*/

#include "postgres.h"
#include "cdb/cdbksegment.h"

static CdbVisitOpt SeqScanFinderWalker(PlanState *node, void *context);

List* cdbCheckPlannerNode(PlanState *planstate, int numSlices, int rootIdx){
    List *noSeqSliceId = NIL;;
    SeqScanFinderContext ctx;

    for (int i = 0; i < numSlices; i++)
    {
        MotionState *motionstate = NULL;
        if (i == rootIdx)
        {
            continue;
        }
	ctx.seq_found = false;
	ctx.motion_found = false;
        motionstate = getMotionState(planstate, i);
        planstate_walk_node(&motionstate->ps, SeqScanFinderWalker, &ctx);
        if (!ctx.seq_found)
        {
            noSeqSliceId = lappend_int(noSeqSliceId, i);
            elog(NOTICE, "no seq slice found in slice %d", i);
        } else {
            elog(NOTICE, "seq slice found in slice %d", i);
        }
    }

    return noSeqSliceId;

}


/**
 * Walker method that finds seq scan node within a planstate tree.
 */
static CdbVisitOpt
SeqScanFinderWalker(PlanState *node,
				  void *context)
{
	Assert(context);
	SeqScanFinderContext *ctx = (SeqScanFinderContext *) context;

	if (IsA(node, SeqScanState))
	{
		ctx->seq_found = true;
		return CdbVisit_Skip;	/* don't visit subtree */
	}
	else if (IsA(node, MotionState) && ctx->motion_found)
	{
		return CdbVisit_Skip;
	}
	else if (IsA(node, MotionState))
	{
		ctx->motion_found = true;
	}


	/* Continue walking */
	return CdbVisit_Walk;
}

