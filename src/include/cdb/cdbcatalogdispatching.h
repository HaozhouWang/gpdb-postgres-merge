#ifndef CDBCATALOGDISPATHING_H
#define CDBCATALOGDISPATHING_H

#include "nodes/execnodes.h"
#include "optimizer/walkers.h"
#include "executor/executor.h"
enum DispathingCatalogInfoObjType
{
	RelationType,
	TablespaceType,
	NamespaceType,
	ProcType,
	AggType,
	LangType,
	TypeType,
	OpType,
	OpClassType
};
typedef enum DispathingCatalogInfoObjType DispathingCatalogInfoObjType;

struct DispathingCatalogInfoContextHashKey
{
	Oid objid;
	DispathingCatalogInfoObjType type;
};
typedef struct DispathingCatalogInfoContextHashKey DispathingCatalogInfoContextHashKey;

struct DispathingCatalogInfoContextHashEntry
{
	DispathingCatalogInfoContextHashKey key;
	Oid aoseg_relid;  /* pg_aoseg_* or pg_aocsseg_* relid */
};

typedef struct DispathingCatalogInfoContextHashEntry DispathingCatalogInfoContextHashEntry;

struct DispatchingCatalogInfo
{
	HTAB		*htab;
	List		*errTblOid;
};

typedef struct DispatchingCatalogInfo DispathingCatalogInfoContext;


bool collect_func_walker(Node *node, DispathingCatalogInfoContext *context);
HTAB * createPrepareDispatchedCatalogRelationDisctinctHashTable(void);
#endif //CDBCATALOGDISPATHING_H
