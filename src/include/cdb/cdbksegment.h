#ifndef CDBKSEG_H
#define CDBKSEG_H

#include "nodes/execnodes.h"

typedef struct SeqScanFinderContext
{
	plan_tree_base_prefix base; /* Required prefix for plan_tree_walker/mutator */
	bool found; /* Input */
} SeqScanFinderContext;


List* cdbCheckPlannerNode(PlanState *planstate, int numSlices);

#endif //CDBKSEG_H