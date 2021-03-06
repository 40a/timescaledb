#include <postgres.h>
#include <access/xact.h>
#include <catalog/namespace.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>

#include "log.h"
#include "scanner.h"
#include "params.h"
#include "catalog.h"

static char *application_name = "unset";

void
bgw_log_set_application_name(char *name)
{
	application_name = name;
}

static bool
bgw_log_insert_relation(Relation rel, char *msg)
{
	TupleDesc	desc = RelationGetDescr(rel);
	static int32 msg_no = 0;
	Datum		values[4];
	bool		nulls[4] = {false, false, false};

	values[0] = Int32GetDatum(msg_no++);
	values[1] = Int64GetDatum((int64) params_get()->current_time);
	values[2] = CStringGetTextDatum(application_name);
	values[3] = CStringGetTextDatum(msg);

	catalog_insert_values(rel, desc, values, nulls);

	return true;
}

/* Insert a new entry into public.bgw_log
 * This table is used for testing as a way for mock background jobs
 * to insert messges into a log that could then be output into the golden file
 */
static void
bgw_log_insert(char *msg)
{
	Relation	rel;
	Oid			log_oid = get_relname_relid("bgw_log", get_namespace_oid("public", false));

	rel = heap_open(log_oid, RowExclusiveLock);
	bgw_log_insert_relation(rel, msg);
	heap_close(rel, RowExclusiveLock);
}

static
emit_log_hook_type prev_emit_log_hook = NULL;

static void
emit_log_hook_callback(ErrorData *edata)
{
	bool		started_txn = false;

	if (!IsTransactionState())
	{
		StartTransactionCommand();
		started_txn = true;
	}

	bgw_log_insert(edata->message);

	if (started_txn)
		CommitTransactionCommand();

	if (prev_emit_log_hook != NULL)
		prev_emit_log_hook(edata);
}

void
register_emit_log_hook()
{
	prev_emit_log_hook = emit_log_hook;
	emit_log_hook = emit_log_hook_callback;
}
