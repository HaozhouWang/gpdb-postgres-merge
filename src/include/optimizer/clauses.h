/*-------------------------------------------------------------------------
 *
 * clauses.h
 *	  prototypes for clauses.c.
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
<<<<<<< HEAD
 * $PostgreSQL: pgsql/src/include/optimizer/clauses.h,v 1.94 2008/08/25 22:42:34 tgl Exp $
=======
 * $PostgreSQL: pgsql/src/include/optimizer/clauses.h,v 1.95 2008/10/09 19:27:40 tgl Exp $
>>>>>>> 38e9348282e
 *
 *-------------------------------------------------------------------------
 */
#ifndef CLAUSES_H
#define CLAUSES_H

#include "nodes/relation.h"
#include "optimizer/walkers.h"


#define is_opclause(clause)		((clause) != NULL && IsA(clause, OpExpr))
#define is_funcclause(clause)	((clause) != NULL && IsA(clause, FuncExpr))
<<<<<<< HEAD

// max size of a folded constant when optimizing queries in Orca
// Note: this is to prevent OOM issues when trying to serialize very large constants
// Current limit: 100KB
#define GPOPT_MAX_FOLDED_CONSTANT_SIZE (100*1024)
=======
>>>>>>> 38e9348282e

typedef struct
{
	int			numAggs;		/* total number of aggregate calls */
	int			numDistinctAggs;	/* number that use DISTINCT */
	Size		transitionSpace;	/* for pass-by-ref transition data */
	List   *dqaArgs;	/* CDB: List of distinct DQA argument exprs. */
	List   *aggOrder;   /* CDB: List of AggOrder clauses */
	bool	missing_prelimfunc; /* CDB: any agg func w/o a prelim func? */
} AggClauseCounts;

/*
 * Representing a canonicalized grouping sets.
 */
typedef struct CanonicalGroupingSets
{
	int num_distcols;   /* number of distinct grouping columns */
	int ngrpsets;   /* number of grouping sets */
	Bitmapset **grpsets;  /* one Bitmapset for each grouping set */
	int *grpset_counts;  /* one for each grouping set, representing the number of times that
						  * each grouping set appears
						  */
} CanonicalGroupingSets;

extern Expr *make_opclause(Oid opno, Oid opresulttype, bool opretset,
			  Expr *leftop, Expr *rightop);
extern Node *get_leftop(Expr *clause);
extern Node *get_rightop(Expr *clause);

extern bool not_clause(Node *clause);
extern Expr *make_notclause(Expr *notclause);
extern Expr *get_notclausearg(Expr *notclause);

extern bool or_clause(Node *clause);
extern Expr *make_orclause(List *orclauses);

extern bool and_clause(Node *clause);
extern Expr *make_andclause(List *andclauses);
extern Node *make_and_qual(Node *qual1, Node *qual2);
extern Expr *make_ands_explicit(List *andclauses);
extern List *make_ands_implicit(Expr *clause);

extern bool contain_agg_clause(Node *clause);
extern void count_agg_clauses(Node *clause, AggClauseCounts *counts);

<<<<<<< HEAD
extern bool contain_window_function(Node *clause);

=======
>>>>>>> 38e9348282e
extern double expression_returns_set_rows(Node *clause);

extern bool contain_subplans(Node *clause);

extern bool contain_mutable_functions(Node *clause);
extern bool contain_volatile_functions(Node *clause);
extern bool contain_nonstrict_functions(Node *clause);
extern Relids find_nonnullable_rels(Node *clause);
extern List *find_nonnullable_vars(Node *clause);
extern List *find_forced_null_vars(Node *clause);
extern Var *find_forced_null_var(Node *clause);
<<<<<<< HEAD

extern char check_execute_on_functions(Node *clause);
=======
>>>>>>> 38e9348282e

extern bool is_pseudo_constant_clause(Node *clause);
extern bool is_pseudo_constant_clause_relids(Node *clause, Relids relids);

extern int	NumRelids(Node *clause);

extern void CommuteOpExpr(OpExpr *clause);
extern void CommuteRowCompareExpr(RowCompareExpr *clause);

extern Node *strip_implicit_coercions(Node *node);

extern void set_coercionform_dontcare(Node *node);

extern Node *eval_const_expressions(PlannerInfo *root, Node *node);

extern Query *fold_constants(PlannerGlobal *glob, Query *q, ParamListInfo boundParams, Size max_size);

extern Expr *transform_array_Const_to_ArrayExpr(Const *c);

extern Node *estimate_expression_value(PlannerInfo *root, Node *node);

<<<<<<< HEAD
extern Query *inline_set_returning_function(PlannerInfo *root, Node *node);

extern Expr *evaluate_expr(Expr *expr, Oid result_type, int32 result_typmod);

extern bool is_grouping_extension(CanonicalGroupingSets *grpsets);
extern bool contain_extended_grouping(List *grp);

extern bool is_builtin_true_equality_between_same_type(int opno);
extern bool is_builtin_greenplum_hashable_equality_between_same_type(int opno);

extern bool subexpression_match(Expr *expr1, Expr *expr2);

// resolve the join alias varno/varattno information to its base varno/varattno information
extern Query *flatten_join_alias_var_optimizer(Query *query, int queryLevel);
=======
extern Query *inline_set_returning_function(PlannerInfo *root,
											RangeTblEntry *rte);
>>>>>>> 38e9348282e

#endif   /* CLAUSES_H */
