
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

enum CatalogDispatchingItemType
{
	TupleType, Namespace
};
static void
prepareDispatchedCatalogFunction(DispathingCatalogInfoContext *ctx, Oid procOid);
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
static void
WriteData(DispathingCatalogInfoContext *ctx, const char *buffer, int size)
{

    Assert(NULL != ctx);
    Assert(NULL != buffer);
    Assert(size > 0);

	if (ctx->cursor + size > ctx->size)
	{
		ctx->size = (1 + ctx->size) * 2;

		if (ctx->size < ctx->cursor + size)
			ctx->size = ctx->cursor + size;

		if (ctx->buffer)
			ctx->buffer = repalloc(ctx->buffer, ctx->size);
		else
			ctx->buffer = palloc(ctx->size);
	}
	memcpy(ctx->buffer + ctx->cursor, buffer, size);
	ctx->cursor += size;
}
void
AddTupleToContextInfo(DispathingCatalogInfoContext *ctx, Oid relid,
        const char *relname, HeapTuple tuple, int32 contentid)
{
    Assert(NULL != ctx);
    Assert(!ctx->finalized);

    StringInfoData header;
    initStringInfo(&header);

    int32 tuplen = sizeof(HeapTupleData) + tuple->t_len;

    /* send one byte flag */
    pq_sendint(&header, TupleType, sizeof(char));

    /* send content length, sizeof(relid) + sizeof(contentid) + tuple */
    int32 len = sizeof(Oid) + sizeof(int32) + tuplen;
    pq_sendint(&header, len, sizeof(int32));

    /* send relid */
    pq_sendint(&header, relid, sizeof(Oid));
    pq_sendint(&header, contentid, sizeof(int32));

    WriteData(ctx, header.data, header.len);
    WriteData(ctx, (const char *) tuple, sizeof(HeapTupleData));
    WriteData(ctx, (const char *) tuple->t_data, tuple->t_len);

    pfree(header.data);
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
	ReleaseSysCache(typetuple);
}

static void
prepareDispatchedCatalogLanguage(DispathingCatalogInfoContext *ctx, Oid langOid)
{
	HeapTuple langtuple;

	Assert(langOid != InvalidOid);

	/*   
	 * buildin object, dispatch nothing
	 */
	if (langOid < FirstNormalObjectId)
		return;

	if (alreadyAddedForDispatching(ctx->htab, langOid, LangType))
		return;

	/* look up the tuple in pg_language */
	langtuple = SearchSysCache1(LANGOID, ObjectIdGetDatum(langOid));

	if (!HeapTupleIsValid(langtuple))
		elog(ERROR, "cache lookup failed for lang %u", langOid);				

	//AddTupleWithToastsToContextInfo(ctx, LanguageRelationId, "pg_language", langtuple, MASTER_CONTENT_ID);	

	/* dispatch the language handler function*/
	bool lang_handler_isNull = false;
	Datum lang_handler_Datum = SysCacheGetAttr(LANGOID, langtuple, Anum_pg_language_lanplcallfoid, &lang_handler_isNull);
	if (!lang_handler_isNull && lang_handler_Datum)	
	{
		prepareDispatchedCatalogFunction(ctx, DatumGetObjectId(lang_handler_Datum));
	}
	ReleaseSysCache(langtuple);
}

static void
prepareDispatchedCatalogFunction(DispathingCatalogInfoContext *ctx, Oid procOid)
{
	HeapTuple proctuple;
	Datum datum;
	Oid oidval;
	bool isNull = false;

    Assert(procOid != InvalidOid);

    /*   
     * buildin object, dispatch nothing
     */
    if (procOid < FirstNormalObjectId)
        return;

    if (alreadyAddedForDispatching(ctx->htab, procOid, ProcType))
        return;

    /* find relid in pg_class */
    proctuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(procOid));
    if (!HeapTupleIsValid(proctuple))
        elog(ERROR, "cache lookup failed for proc %u", procOid);

	datum = SysCacheGetAttr(PROCOID, proctuple, Anum_pg_proc_prolang, &isNull);
	if (!isNull && datum)
	{
		oidval = DatumGetObjectId(datum);
		prepareDispatchedCatalogLanguage(ctx, oidval);
	}

	datum = SysCacheGetAttr(PROCOID, proctuple, Anum_pg_proc_prorettype, &isNull);
	if (!isNull && datum)
	{
		oidval = DatumGetObjectId(datum);
		prepareDispatchedCatalogType(ctx, oidval);
	}

	AddTupleToContextInfo(ctx, ProcedureRelationId, "pg_proc", proctuple, MASTER_CONTENT_ID);
	ReleaseSysCache(proctuple);
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
	if (IsA(node, FuncExpr))
	{
		FuncExpr *func = (FuncExpr *) node;
		AclMode needAcl = ACL_NO_RIGHTS;
		if(func->funcid > FirstNormalObjectId)
		{
			/* do aclcheck on master, because HAWQ segments lacks user knowledge */
			AclResult aclresult;
			aclresult = pg_proc_aclcheck(func->funcid, GetUserId(), ACL_EXECUTE);
			if (aclresult != ACLCHECK_OK)
				aclcheck_error(aclresult, OBJECT_FUNCTION, get_func_name(func->funcid));

			/* build the dispacth for the function itself */
			prepareDispatchedCatalogFunction(context, func->funcid);
		}
	}
	//TODO Aggregation Node
	//TODO window function
	//TODO Op node
	//TODO sort node
	//TODO group node
	return plan_tree_walker(node, collect_func_walker, context, false);
}