#ifndef CDBKSEG_H
#define CDBKSEG_H

#include "nodes/execnodes.h"
#include "optimizer/walkers.h"
#include "executor/executor.h"
#include "cdb/cdbcatalogdispatching.h"
typedef struct SeqScanFinderContext
{
	plan_tree_base_prefix base; /* Required prefix for plan_tree_walker/mutator */
	bool seq_found; /* Input */
	bool motion_found; /* Input */
	DispathingCatalogInfoContext catalog;
} SeqScanFinderContext;


List* cdbCheckPlannerNode(PlanState *planstate, int numSlices, int rootIdx);

#endif //CDBKSEG_H
