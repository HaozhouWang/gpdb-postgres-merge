/*-------------------------------------------------------------------------
 *
 * prep.h
 *	  prototypes for files in optimizer/prep/
 *
 *
 * Portions Copyright (c) 2006-2008, Greenplum inc
 * Portions Copyright (c) 2012-Present Pivotal Software, Inc.
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
<<<<<<< HEAD
 * $PostgreSQL: pgsql/src/include/optimizer/prep.h,v 1.61 2008/08/14 18:48:00 tgl Exp $
=======
 * $PostgreSQL: pgsql/src/include/optimizer/prep.h,v 1.63 2008/11/11 18:13:32 tgl Exp $
>>>>>>> 38e9348282e
 *
 *-------------------------------------------------------------------------
 */
#ifndef PREP_H
#define PREP_H

#include "nodes/plannodes.h"
#include "nodes/relation.h"


/*
 * prototypes for prepjointree.c
 */
<<<<<<< HEAD
=======
extern void pull_up_sublinks(PlannerInfo *root);
>>>>>>> 38e9348282e
extern void inline_set_returning_functions(PlannerInfo *root);
extern void pull_up_sublinks(PlannerInfo *root);
extern Node *pull_up_subqueries(PlannerInfo *root, Node *jtnode,
				   JoinExpr *lowest_outer_join,
					AppendRelInfo *containing_appendrel);
extern void reduce_outer_joins(PlannerInfo *root);
extern Relids get_relids_in_jointree(Node *jtnode, bool include_joins);
extern Relids get_relids_for_join(PlannerInfo *root, int joinrelid);

extern List *init_list_cteplaninfo(int numCtes);

/*
 * prototypes for prepqual.c
 */
extern Expr *canonicalize_qual(Expr *qual);

/*
 * prototypes for preptlist.c
 */
extern List *preprocess_targetlist(PlannerInfo *root, List *tlist);

/*
 * prototypes for prepunion.c
 */
extern Plan *plan_set_operations(PlannerInfo *root, double tuple_fraction,
					List **sortClauses);

extern List *find_all_inheritors(Oid parentrel);

extern void expand_inherited_tables(PlannerInfo *root);

<<<<<<< HEAD
extern Node *adjust_appendrel_attrs(PlannerInfo *root, Node *node, AppendRelInfo *appinfo);
=======
extern Node *adjust_appendrel_attrs(Node *node, AppendRelInfo *appinfo);
>>>>>>> 38e9348282e

#endif   /* PREP_H */
