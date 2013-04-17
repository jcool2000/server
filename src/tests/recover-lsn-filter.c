// this test makes sure the LSN filtering is used during recovery

#include <sys/stat.h>
#include <sys/wait.h>
#include "test.h"

const int envflags = DB_INIT_MPOOL|DB_CREATE|DB_THREAD |DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN;

char *namea="a.db";

static void checkpoint_callback_2_do_abort(void *UU(extra)) {
    abort();
}

static void run_test (void) {
    int r;

    system("rm -rf " ENVDIR);
    toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO);

    db_env_set_checkpoint_callback2(checkpoint_callback_2_do_abort, NULL);

    DB_ENV *env;
    r = db_env_create(&env, 0);                                                         CKERR(r);
    r = env->open(env, ENVDIR, envflags, S_IRWXU+S_IRWXG+S_IRWXO);                      CKERR(r);

    DB *db;
    r = db_create(&db, env, 0);                                                         CKERR(r);
    r = db->open(db, NULL, namea, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);      CKERR(r);

    // txn_begin; insert <a,a>; txn_abort
    {
        DB_TXN *txn;
        r = env->txn_begin(env, NULL, &txn, 0);                                         CKERR(r);
	DBT k={.data="a", .size=2};
	DBT v={.data="a", .size=2};
	r = db->put(db, txn, &k, &v, DB_YESOVERWRITE);                                  CKERR(r);
        r = txn->abort(txn);                                                            CKERR(r);
    }

    // txn_begin; insert <a,b>;
    {
        DB_TXN *txn;
        r = env->txn_begin(env, NULL, &txn, 0);                                         CKERR(r);
	DBT k={.data="a", .size=2};
	DBT v={.data="b", .size=2};
	r = db->put(db, txn, &k, &v, DB_YESOVERWRITE);                                  CKERR(r);
    }

    // checkpoint
    r = env->txn_checkpoint(env, 0, 0, 0);                                              CKERR(r);

    // abort the process
    abort();
}

static void run_recover (void) {
    DB_ENV *env;
    int r;

    // strip the end_checkpoint from the log.  this will force recovery to run the over
    // the entire log.
    // TODO need an API to do this
    struct stat s;
    r = toku_stat(ENVDIR "/log000000000000.tokulog", &s);                               CKERR(r);
    r = truncate(ENVDIR "/log000000000000.tokulog", s.st_size - 0x25);                  CKERR(r);

    // run recovery
    r = db_env_create(&env, 0);                                                         CKERR(r);
    r = env->open(env, ENVDIR, envflags + DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);         CKERR(r);

    // verify the data
    DB *db;
    r = db_create(&db, env, 0);                                                         CKERR(r);
    r = db->open(db, NULL, namea, NULL, DB_UNKNOWN, DB_AUTO_COMMIT, 0666);              CKERR(r);
    
    DB_TXN *txn;
    r = env->txn_begin(env, NULL, &txn, 0);                                             CKERR(r);
    DBC *cursor;
    r = db->cursor(db, txn, &cursor, 0);                                                CKERR(r);
    DBT k, v;
    r = cursor->c_get(cursor, dbt_init_malloc(&k), dbt_init_malloc(&v), DB_FIRST);
    assert(r == DB_NOTFOUND);

    r = cursor->c_close(cursor);                                                          CKERR(r);

    r = env->close(env, 0);                                                             CKERR(r);
    exit(0);
}

static void run_no_recover (void) {
    DB_ENV *env;
    int r;

    r = db_env_create(&env, 0);                                                         CKERR(r);
    r = env->open(env, ENVDIR, envflags & ~DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);        CKERR(r);
    r = env->close(env, 0);                                                             CKERR(r);
    exit(0);
}

const char *cmd;

BOOL do_test=FALSE, do_recover=FALSE, do_recover_only=FALSE, do_no_recover = FALSE;

static void test_parse_args (int argc, char *argv[]) {
    int resultcode;
    cmd = argv[0];
    argc--; argv++;
    while (argc>0) {
	if (strcmp(argv[0], "-v") == 0) {
	    verbose++;
	} else if (strcmp(argv[0],"-q")==0) {
	    verbose--;
	    if (verbose<0) verbose=0;
	} else if (strcmp(argv[0], "--test")==0) {
	    do_test=TRUE;
        } else if (strcmp(argv[0], "--recover") == 0) {
            do_recover=TRUE;
        } else if (strcmp(argv[0], "--recover-only") == 0) {
            do_recover_only=TRUE;
        } else if (strcmp(argv[0], "--no-recover") == 0) {
            do_no_recover=TRUE;
	} else if (strcmp(argv[0], "-h")==0) {
	    resultcode=0;
	do_usage:
	    fprintf(stderr, "Usage:\n%s [-v|-q]* [-h] {--test | --recover } \n", cmd);
	    exit(resultcode);
	} else {
	    fprintf(stderr, "Unknown arg: %s\n", argv[0]);
	    resultcode=1;
	    goto do_usage;
	}
	argc--;
	argv++;
    }
}

int test_main (int argc, char *argv[]) {
    test_parse_args(argc, argv);
    if (do_test) {
	run_test();
    } else if (do_recover) {
        run_recover();
    } else if (do_recover_only) {
        run_recover();
    } else if (do_no_recover) {
        run_no_recover();
    } 
    return 0;
}
