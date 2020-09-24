
#include "postgres.h"

#include <sys/fcntl.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/tuptoaster.h"
#include "catalog/aoseg.h"
#include "catalog/catalog.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_language.h"
#include "catalog/pg_type.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_attribute_encoding.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_magic_oid.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_tablespace.h"
#include "catalog/toasting.h"
#include "cdb/cdbvars.h"
#include "commands/dbcommands.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"
#include "optimizer/prep.h"
#include "storage/fd.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "utils/uri.h"
#include "nodes/execnodes.h"
#include "cdb/cdbcatalogdispatching.h"

static bool
alreadyAddedForDispatching(HTAB *rels, Oid objid, DispathingCatalogInfoObjType type) {
	bool found = false;

	Assert(NULL != rels);

	DispathingCatalogInfoContextHashKey item;

	item.objid = objid;
	item.type = type;

	hash_search(rels, &item, HASH_ENTER, &found);

	return found;
}
HTAB *
createPrepareDispatchedCatalogRelationDisctinctHashTable(void)
{
	HASHCTL info;
	HTAB *rels = NULL;

	MemSet(&info, 0, sizeof(info));

	info.hash = tag_hash;
	info.keysize = sizeof(DispathingCatalogInfoContextHashKey);
	info.entrysize = sizeof(DispathingCatalogInfoContextHashEntry);

	rels = hash_create("all relations", 10, &info, HASH_FUNCTION | HASH_ELEM);

	return rels;
}


/* For user defined type dispatch the operator class. */
static void
prepareDispatchedCatalogOpClassForType(DispathingCatalogInfoContext *ctx, Oid typeOid)
{
	return;
}

static void
prepareDispatchedCatalogType(DispathingCatalogInfoContext *ctx, Oid typeOid)
{
	HeapTuple typetuple;
	bool isNull = false;
	Datum typeDatum = 0;

	Assert(typeOid != InvalidOid);

	/*
	 * buildin object, dispatch nothing
	 */
	if (typeOid < FirstNormalObjectId)
		return;

	if (alreadyAddedForDispatching(ctx->htab, typeOid, TypeType))
		return;


	typetuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typeOid));
	if (!HeapTupleIsValid(typetuple))
		elog(ERROR, "cache lookup failed for type %u", typeOid);

	/*
	 * In case of arrays of user defined types we should also
	 * dispatch the underlying type defined by typelem attribute
	 * if it is also a user defined type.
	 */
	typeDatum = SysCacheGetAttr(TYPEOID, typetuple, Anum_pg_type_typacl,
								   &isNull);
	if (!isNull && OidIsValid(DatumGetObjectId(typeDatum)))
	{
		prepareDispatchedCatalogType(ctx, DatumGetObjectId(typeDatum));
	}

	/*
	 TODO We need to dispatch all the accompanying functions for user defined types.
	 input, output, receive, send, typetuple, relid
	 */


	/*
	 * For user defined types we also dispatch associated operator classes.
	 * This is to safe guard against certain cases like Sort Operator and
	 * Hash Aggregates where the plan tree does not explicitly contain all the
	 * operators that might be needed by the executor on the segments.
	 */
	prepareDispatchedCatalogOpClassForType(ctx, typeOid);
}


bool collect_func_walker(Node *node, DispathingCatalogInfoContext *context)
{
	if (node == NULL)
		return false;

	//TODO Subplan

	if (IsA(node, Const))
	{
		/*
		 *When constants are part of the expression we should dispacth
		 *the type of the constant.
		 */
		Const *var = (Const *) node;
		prepareDispatchedCatalogType(context, var->consttype);
	}

	if (IsA(node, Var))
	{
		/*
		 *When constants are part of the expression we should dispacth
		 *the type of the constant.
		 */
		Var *var = (Var *) node;
		prepareDispatchedCatalogType(context, var->vartype);
	}
	//TODO Row expr
	//TODO Function node
	//TODO Aggregation Node
	//TODO window function
	//TODO Op node
	//TODO sort node
	//TODO group node
	return plan_tree_walker(node, collect_func_walker, context, false);
}