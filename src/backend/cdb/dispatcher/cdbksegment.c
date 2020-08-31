
/*
   placeholder
*/

#include "postgres.h"
#include "cdb/cdbksegment.h"

List* cdbCheckPlannerNode(PlanState *planstate, int numSlices, rootIdx){
    List *noSeqSliceId = NIL;;
    int rootIdx;
    MotionState *motionstate = NULL;
    SeqScanFinderContext ctx;

    noSeqSliceId = new_list(T_IntList);


    for (int i = 0; i < numSlices; i++)
    {
        MotionState *motionstate = NULL;
        if (i == rootIdx)
        {
            continue;
        }
        motionstate = getMotionState(planstate, i);
        planstate_walk_node(motionstate->ps, SeqScanFinderWalker, &ctx);
        if (!ctx.found)
        {
            noSeqSliceId = lappend_int(noSeqSliceId, i);
            elog(NOTICE, "no seq slice found in slice %d", i);
        } else {
            elog(NOTICE, "seq slice found in slice %d", i);
        }
    }

    return noSeqSliceId;

}


/*
 * Walker to find a motion node that matches a particular motionID
 */
static bool
SeqScanFinderWalker(Plan *node,
				  void *context)
{
	Assert(context);
	SeqScanFinderContext *ctx = (SeqScanFinderContext *) context;


	if (node == NULL)
        ctx->found = false;
		return false;

	if (IsA(node, SeqScan))
	{
		ctx->found = true;
        return true;	/* found our node; no more visit */
	}

	/* Continue walking */
	return plan_tree_walker((Node*)node, SeqScanFinderWalker, ctx, true);
}