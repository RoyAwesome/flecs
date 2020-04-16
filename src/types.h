#ifndef FLECS_TYPES_PRIVATE_H
#define FLECS_TYPES_PRIVATE_H

#ifndef __MACH__
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

#ifdef _MSC_VER
//FIXME
#else
#include <sys/param.h>  /* attempt to define endianness */
#endif
#ifdef linux
# include <endian.h>    /* attempt to define endianness */
#endif

#include "flecs.h"
#include "flecs/util/dbg.h"
#include "flecs/util/entity_index.h"

#define ECS_MAX_JOBS_PER_WORKER (16)

/** Entity id's higher than this number will be stored in a map instead of a
 * sparse set. Increasing this value can improve performance at the cost of
 * (significantly) higher memory usage. */
#define ECS_HI_ENTITY_ID (100000)

/** This reserves entity ids for components. Regular entity ids will start after
 * this constant. This affects performance of table traversal, as edges with ids 
 * lower than this constant are looked up in an array, whereas constants higher
 * than this id are looked up in a map. Increasing this value can improve
 * performance at the cost of (significantly) higher memory usage. */
#define ECS_HI_COMPONENT_ID (256) /* Maximum number of components */

/* This is _not_ the max number of entities that can be of a given type. This 
 * constant defines the maximum number of components, prefabs and parents can be
 * in one type. This limit serves two purposes: detect errors earlier (assert on
 * very large types) and allow for more efficient allocation strategies (like
 * using alloca for temporary buffers). */
#define ECS_MAX_ENTITIES_IN_TYPE (256)

/** These values are used to verify validity of the pointers passed into the API
 * and to allow for passing a thread as a world to some API calls (this allows
 * for transparently passing thread context to API functions) */
#define ECS_WORLD_MAGIC (0x65637377)
#define ECS_THREAD_MAGIC (0x65637374)


/* -- Builtin component types -- */

/* For prefabs with child entities, the parent prefab must be marked so that
 * flecs knows not to share components from it, as adding a prefab as a parent
 * is stored in the same way as adding a prefab for sharing components.
 * There are two mechanisms required to accomplish this. The first one is to set
 * the 'parent' member in the EcsPrefab component, for the child entity of the
 * prefab. This acts as a front-end for another mechanism, that ensures that
 * child entities for different prefab parents are added to different tables. As
 * a result of setting a parent in EcsPrefab, Flecs will:
 * 
 *  - Add the prefab to the entity type
 *  - Find or create a prefab parent flag entity
 *  - Set the EcsPrefabParent component on the prefab parent flag entity
 *  - Add the prefab parent flag entity to the child
 * 
 * The last step ensures that the type of the child entity is associated with at
 * most one prefab parent. If the mechanism would just rely on the EcsPrefab
 * parent field, it would theoretically be possible that childs for different
 * prefab parents end up in the same table.
 */
typedef struct EcsPrefabParent {
    ecs_entity_t parent;
} EcsPrefabParent;

typedef struct ecs_builder_op_t {
    const char *id;
    ecs_type_t type;
} ecs_builder_op_t;

typedef struct EcsPrefabBuilder {
    ecs_vector_t *ops; /* ecs_builder_op_t */
} EcsPrefabBuilder;

typedef enum ecs_sig_inout_kind_t {
    EcsInOut,
    EcsIn,
    EcsOut
} ecs_sig_inout_kind_t;

/** Type that is used by systems to indicate where to fetch a component from */
typedef enum ecs_sig_from_kind_t {
    EcsFromSelf,            /* Get component from self (default) */
    EcsFromOwned,           /* Get owned component from self */
    EcsFromShared,          /* Get shared component from self */
    EcsFromContainer,       /* Get component from container */
    EcsFromSystem,          /* Get component from system */
    EcsFromEmpty,           /* Get entity handle by id */
    EcsFromEntity,          /* Get component from other entity */
    EcsCascade              /* Walk component in cascading (hierarchy) order */
} ecs_sig_from_kind_t;

/** Type describing an operator used in an signature of a system signature */
typedef enum ecs_sig_oper_kind_t {
    EcsOperAnd = 0,
    EcsOperOr = 1,
    EcsOperNot = 2,
    EcsOperOptional = 3,
    EcsOperLast = 4
} ecs_sig_oper_kind_t;

/** Callback used by the system signature expression parser */
typedef int (*ecs_parse_action_t)(
    ecs_world_t *world,
    const char *id,
    const char *expr,
    int column,
    ecs_sig_from_kind_t from_kind,
    ecs_sig_oper_kind_t oper_kind,
    ecs_sig_inout_kind_t inout_kind,
    const char *component,
    const char *source,
    void *ctx);

/** Type that describes a single column in the system signature */
typedef struct ecs_sig_column_t {
    ecs_sig_from_kind_t from_kind;        /* Element kind (Entity, Component) */
    ecs_sig_oper_kind_t oper_kind;   /* Operator kind (AND, OR, NOT) */
    ecs_sig_inout_kind_t inout_kind; /* Is component read or written */
    union {
        ecs_type_t type;             /* Used for OR operator */
        ecs_entity_t component;      /* Used for AND operator */
    } is;
    ecs_entity_t source;             /* Source entity (used with FromEntity) */
} ecs_sig_column_t;

/** A component array in a table */
struct ecs_column_t {
    ecs_vector_t *data;              /* Column data */
    uint16_t size;                   /* Column size (saves component lookups) */
};

/** Table component data and entity ids */
struct ecs_data_t {
    ecs_vector_t *entities;
    ecs_vector_t *record_ptrs;
    ecs_column_t *columns;
};

#define EcsTableIsStaged (1)
#define EcsTableIsPrefab (2)
#define EcsTableHasPrefab (4)
#define EcsTableHasBuiltins (8)

/** Edge used for traversing the table graph */
typedef struct ecs_edge_t {
    ecs_table_t *add;
    ecs_table_t *remove;
} ecs_edge_t;

/** A table is the Flecs equivalent of an archetype. Tables store all entities
 * with a specific set of components. Tables are automatically created when an
 * entity has a set of components not previously observed before. When a new
 * table is created, it is automatically matched with existing column systems */
struct ecs_table_t {
    ecs_vector_t *stage_data;         /* Data per stage */
    ecs_vector_t *queries;            /* Queries matched with table */
    ecs_type_t type;                  /* Identifies table type in type_index */
    ecs_edge_t *lo_edges;             /* Edges to low entity ids */
    ecs_map_t *hi_edges;              /* Edges to high entity ids */

    ecs_vector_t *dst_rows;           /* Used for more efficient merging */
    ecs_vector_t *on_new;             /* Systems executed when new entity is
                                       * added to table */

    int32_t flags;                    /* Flags for testing table properties */
    int32_t parent_count;             /* Number of parents in table type */
};

/** Cached reference to a component in an entity */
struct ecs_reference_t {
    ecs_entity_t entity;
    ecs_entity_t component;
    void *cached_ptr;
};

/** Type containing data for a table matched with a system */
typedef struct ecs_matched_table_t {
    ecs_table_t *table;             /* Reference to the table */
    int32_t *columns;               /* Mapping of system columns to table */
    ecs_entity_t *components;       /* Actual components of system columns */
    ecs_vector_t *references;       /* Reference columns and cached pointers */
    int32_t depth;                  /* Depth of table (when using CASCADE) */
} ecs_matched_table_t;

/** Keep track of how many [in] columns are active for [out] columns of OnDemand
 * systems. */
typedef struct ecs_on_demand_out_t {
    ecs_entity_t system;    /* Handle to system */
    int32_t count;         /* Total number of times [out] columns are used */
} ecs_on_demand_out_t;

/** Keep track of which OnDemand systems are matched with which [in] columns */
typedef struct ecs_on_demand_in_t {
    int32_t count;         /* Number of active systems with [in] column */
    ecs_vector_t *systems;  /* Systems that have this column as [out] column */
} ecs_on_demand_in_t;

/** Type that stores a parsed signature */
typedef struct ecs_sig_t {
    const char *name;           /* Optional name used for debugging */
    char *expr;                 /* Original expression string */
    ecs_vector_t *columns;      /* Columns that contain parsed data */
    int32_t cascade_by;         /* Identify CASCADE column */
    bool match_prefab;          /* Does signature match prefabs */
    bool match_disabled;        /* Does signature match disabled */
    bool has_refs;              /* Does signature have references */
    bool needs_tables;          /* Does signature match with tables */

    /* Precomputed types for quick comparisons */
    ecs_type_t not_from_self;      /* Exclude components from self */
    ecs_type_t not_from_owned;     /* Exclude components from self only if owned */
    ecs_type_t not_from_shared;    /* Exclude components from self only if shared */
    ecs_type_t not_from_container; /* Exclude components from components */
    ecs_type_t and_from_self;      /* Which components are required from entity */
    ecs_type_t and_from_owned;     /* Which components are required from entity */
    ecs_type_t and_from_shared;    /* Which components are required from entity */
    ecs_type_t and_from_system;    /* Used to auto-add components to system */
    ecs_type_t and_from_container; /* Used to auto-add components to system */
} ecs_sig_t;

/** Query that is automatically matched against active tables */
struct ecs_query_t {
    /* Signature of query */
    ecs_sig_t sig;

    /* Reference to world */
    ecs_world_t *world;

    /* Tables matched with query */
    ecs_vector_t *tables;
    ecs_vector_t *inactive_tables;

    /* Handle to system (optional) */
    ecs_entity_t system;        
};

/** Base type for a system */
typedef struct EcsSystem {
    ecs_system_action_t action;    /* Callback to be invoked for matching rows */
    void *ctx;                     /* Userdata for system */
    ecs_system_kind_t kind;        /* Kind of system */
    float time_spent;              /* Time spent on running system */
    int32_t invoke_count;          /* Number of times system is invoked */
    bool enabled;                  /* Is system enabled or not */
} EcsSystem;

/** A column system is a system that is ran periodically (default = every frame)
 * on all entities that match the system signature expression. Column systems
 * are prematched with tables (archetypes) that match the system signature
 * expression. Each time a column system is invoked, it iterates over the 
 * matched list of tables (the 'tables' member). 
 *
 * For each table, the system stores a list of the components that were matched
 * with the system. This list may differ from the component list of the table,
 * when OR expressions or optional expressions are used.
 * 
 * A column system keeps track of tables that are empty. These tables are stored
 * in the 'inactive_tables' array. This prevents the system from iterating over
 * tables in the main loop that have no data.
 * 
 * For each table, a column system stores an index that translates between the
 * a column in the system signature, and the matched table. This information is
 * stored, alongside with an index that identifies the table, in the 'tables'
 * member. This is an array of an array of integers, per table.
 * 
 * Additionally, the 'tables' member contains information on where a component
 * should be fetched from. By default, components are fetched from an entity,
 * but a system may specify that a component must be resolved from a container,
 * or must be fetched from a prefab. In this case, the index to lookup a table
 * column from system column contains a negative number, which identifies an
 * element in the 'refs' array.
 * 
 * The 'refs' array contains elements of type 'EcsRef', and stores references
 * to external entities. References can vary per table, but not per entity/row,
 * as prefabs / containers are part of the entity type, which in turn 
 * identifies the table in which the entity is stored.
 * 
 * The 'period' and 'time_passed' members are used for periodic systems. An
 * application may specify that a system should only run at a specific interval, 
 * like once per second. This interval is stored in the 'period' member. Each
 * time the system is evaluated but not ran, the delta_time is added to the 
 * time_passed member, until it exceeds 'period'. In that case, the system is
 * ran, and 'time_passed' is decreased by 'period'. 
 */
typedef struct EcsColSystem {
    EcsSystem base;
    ecs_entity_t entity;                  /* Entity id of system, used for ordering */
    ecs_query_t *query;                   /* System query */
    ecs_vector_t *jobs;                   /* Jobs for this system */
    ecs_on_demand_out_t *on_demand;       /* Keep track of [out] column refs */
    ecs_system_status_action_t status_action; /* Status action */
    void *status_ctx;                     /* User data for status action */    
    ecs_entity_t tick_source;             /* Tick source associated with system */
    float time_passed;                    /* Time passed since last invocation */
    bool enabled_by_demand;               /* Is system enabled by on demand systems */
    bool enabled_by_user;                 /* Is system enabled by user */
} EcsColSystem;
 
/** A row system is a system that is ran on 1..n entities for which a certain 
 * operation has been invoked. The system kind determines on what kind of
 * operation the row system is invoked. Example operations are ecs_add,
 * ecs_remove and ecs_set. */
typedef struct EcsRowSystem {
    EcsSystem base;
    ecs_sig_t sig;            /* System signature */
    ecs_vector_t *components;       /* Components in order of signature */
} EcsRowSystem;
 
#define ECS_TYPE_DB_MAX_CHILD_NODES (256)
#define ECS_TYPE_DB_BUCKET_COUNT (256)

/** The ecs_type_node_t type is a node in a hierarchical structure that allows
 * for quick lookups of types. A node represents a type, and its direct children
 * represent types with one additional entity. For example, given a node [A],
 * [A, B] would be a child node.
 * 
 * Child nodes are looked up directly using the entity id. For example, node [A]
 * will be stored at root.nodes[A]. Children entity ids are offset by their 
 * parent, such that [A, B] is stored at root.nodes[A].nodes[B - A].
 * 
 * If the offset exceeds ECS_TYPE_DB_MAX_CHILD_NODES, the type will be stored in
 * the types map. This map is keyed by the hash of the type relative to its
 * parent. For example, the hash for type [A, B, C] will be computed on [B, C]
 * if its parent is [A]. */
typedef struct ecs_type_link_t {
    ecs_type_t type;                /* type of current node */
    struct ecs_type_link_t *next;   /* next link (for iterating linearly) */
} ecs_type_link_t;

typedef struct ecs_type_node_t {
    ecs_vector_t *nodes;    /* child nodes - <ecs_entity_t, ecs_type_node_t> */
    ecs_vector_t **types;   /* child types w/large entity offsets - <hash, vector<ecs_type_link_t>> */
    ecs_type_link_t link;     
} ecs_type_node_t;

struct ecs_ei_t {
    ecs_sparse_t *lo;       /* Low entity ids are stored in a sparse set */
    ecs_map_t *hi;          /* To save memory high ids are stored in a map */
    bool keep_deletes;      /* Insert empty record for deletes */
};

/** A stage is a data structure in which delta's are stored until it is safe to
 * merge those delta's with the main world stage. A stage allows flecs systems
 * to arbitrarily add/remove/set components and create/delete entities while
 * iterating. Additionally, worker threads have their own stage that lets them
 * mutate the state of entities without requiring locks. */
typedef struct ecs_stage_t {
    /* If this is not main stage, 
     * changes to the entity index 
     * are buffered here */
    ecs_ei_t entity_index; /* Entity lookup table for (table, row) */

    /* If this is not a thread
     * stage, these are the same
     * as the main stage */
    ecs_type_node_t type_root;     /* Hierarchical type store (& first link) */
    ecs_type_link_t *last_link;    /* Link to last registered type */
    ecs_sparse_t *tables;          /* Tables created while >1 threads running */
    ecs_map_t *table_index;        /* Lookup table by type */

    ecs_table_t root;              /* Root table */
    ecs_vector_t *dirty_tables;

    int32_t id;                    /* Unique id that identifies the stage */
    
    /* Is entity range checking enabled? */
    bool range_check_enabled;
} ecs_stage_t;

/** Supporting type to store looked up or derived entity data */
typedef struct ecs_entity_info_t {
    ecs_record_t *record;       /* Main stage record in entity index */
    ecs_table_t *table;         /* Table. Not set if entity is empty */
    ecs_data_t *data;           /* Stage-specific table columns */
    int32_t row;                /* Actual row (stripped from is_watched bit) */
    bool is_watched;            /* Is entity being watched */
} ecs_entity_info_t;

/** A type describing a unit of work to be executed by a worker thread. */ 
typedef struct ecs_job_t {
    ecs_entity_t system;          /* System handle */
    EcsColSystem *system_data;    /* System to run */
    int32_t offset;              /* Start index in row chunk */
    int32_t limit;               /* Total number of rows to process */
} ecs_job_t;

/** A type desribing a worker thread. When a system is invoked by a worker
 * thread, it receives a pointer to an ecs_thread_t instead of a pointer to an 
 * ecs_world_t (provided by the ecs_rows_t type). When this ecs_thread_t is passed down
 * into the flecs API, the API functions are able to tell whether this is an
 * ecs_thread_t or an ecs_world_t by looking at the 'magic' number. This allows the
 * API to transparently resolve the stage to which updates should be written,
 * without requiring different API calls when working in multi threaded mode. */
typedef struct ecs_thread_t {
    int32_t magic;                           /* Magic number to verify thread pointer */
    int32_t job_count;                       /* Number of jobs scheduled for thread */
    ecs_world_t *world;                       /* Reference to world */
    ecs_job_t *jobs[ECS_MAX_JOBS_PER_WORKER]; /* Array with jobs */
    ecs_stage_t *stage;                       /* Stage for thread */
    ecs_os_thread_t thread;                   /* Thread handle */
    uint16_t index;                           /* Index of thread */
} ecs_thread_t;

/* World snapshot */
struct ecs_snapshot_t {
    ecs_ei_t entity_index;
    ecs_sparse_t *tables;
    ecs_entity_t last_handle;
    ecs_filter_t filter;
};

/** Component-specific data */
typedef struct ecs_component_data_t {
    ecs_vector_t *on_add;       /* Systems ran after adding this component */
    ecs_vector_t *on_remove;    /* Systems ran after removing this component */
    ecs_vector_t *on_set;       /* Systems ran after setting this component */

    ecs_init_t init;            /* Invoked for new uninitialized component */
    ecs_init_t fini;            /* Invoked when component is deinitialized */
    ecs_replace_t replace;      /* Invoked when component value is replaced */
    ecs_merge_t merge;          /* Invoked when component value is merged */

    void *ctx;
} ecs_component_data_t;

/** The world stores and manages all ECS data. An application can have more than
 * one world, but data is not shared between worlds. */
struct ecs_world_t {
    int32_t magic;               /* Magic number to verify world pointer */
    float delta_time;             /* Time passed to or computed by ecs_progress */
    void *context;                /* Application context */

    ecs_vector_t *component_data;

    /* -- Column systems -- */

    ecs_vector_t *on_load_systems;  
    ecs_vector_t *post_load_systems;  
    ecs_vector_t *pre_update_systems;  
    ecs_vector_t *on_update_systems;   
    ecs_vector_t *on_validate_systems; 
    ecs_vector_t *post_update_systems; 
    ecs_vector_t *pre_store_systems; 
    ecs_vector_t *on_store_systems;   
    ecs_vector_t *manual_systems;  
    ecs_vector_t *inactive_systems;

    /* --  Queries -- */

    ecs_sparse_t *queries;    

    /* -- OnDemand systems -- */
    
    ecs_map_t *on_activate_components; /* Trigger on activate of [in] column */
    ecs_map_t *on_enable_components; /* Trigger on enable of [in] column */


    /* -- Row systems -- */

    ecs_vector_t *add_systems;        /* Systems invoked on ecs_stage_add */
    ecs_vector_t *remove_systems;     /* Systems invoked on ecs_stage_remove */
    ecs_vector_t *set_systems;        /* Systems invoked on ecs_set */


    /* -- Tasks -- */

    ecs_vector_t *fini_tasks;         /* Tasks to execute on ecs_fini */


    /* -- Lookup Indices -- */

    ecs_map_t *type_sys_add_index;    /* Index to find add row systems for type */
    ecs_map_t *type_sys_remove_index; /* Index to find remove row systems for type*/
    ecs_map_t *type_sys_set_index;    /* Index to find set row systems for type */
    
    ecs_map_t *prefab_parent_index;   /* Index to find flag for prefab parent */
    ecs_map_t *type_handles;          /* Handles to named types */


    /* -- Staging -- */

    ecs_stage_t stage;               /* Main storage */
    ecs_stage_t temp_stage;          /* Stage for when processing systems */
    ecs_vector_t *worker_stages;     /* Stages for worker threads */
    uint32_t stage_count;            /* Number of stages in world */


    /* -- Multithreading -- */

    ecs_vector_t *worker_threads;    /* Worker threads */
    ecs_os_cond_t thread_cond;       /* Signal that worker threads can start */
    ecs_os_mutex_t thread_mutex;     /* Mutex for thread condition */
    ecs_os_cond_t job_cond;          /* Signal that worker thread job is done */
    ecs_os_mutex_t job_mutex;        /* Mutex for protecting job counter */
    int32_t jobs_finished;          /* Number of jobs finished */
    int32_t threads_running;        /* Number of threads running */

    ecs_entity_t last_handle;        /* Last issued handle */
    ecs_entity_t min_handle;         /* First allowed handle */
    ecs_entity_t max_handle;         /* Last allowed handle */


    /* -- Handles to builtin components types -- */

    ecs_type_t t_component;
    ecs_type_t t_type;
    ecs_type_t t_prefab;
    ecs_type_t t_row_system;
    ecs_type_t t_col_system;
    ecs_type_t t_builtins;


    /* -- Time management -- */

    ecs_time_t world_start_time;  /* Timestamp of simulation start */
    ecs_time_t frame_start_time;  /* Timestamp of frame start */
    float target_fps;             /* Target fps */
    float fps_sleep;              /* Sleep time to prevent fps overshoot */

    ecs_entity_t add_tick_source; /* System to add EcsTickSource */
    ecs_entity_t progress_timers; /* System to progress timers */
    ecs_entity_t progress_rate_filters; /* System to progress rate filters */


    /* -- Metrics -- */

    double frame_time_total;      /* Total time spent in processing a frame */
    double system_time_total;     /* Total time spent in periodic systems */
    double merge_time_total;      /* Total time spent in merges */
    double world_time_total;      /* Time elapsed since first frame */
    int32_t frame_count_total;   /* Total number of frames */


    /* -- Settings from command line arguments -- */

    int arg_fps;
    int arg_threads;

    /* -- World lock -- */
    ecs_os_mutex_t mutex;         /* Locks the world if locking enabled */

    /* -- World state -- */

    bool valid_schedule;          /* Is job schedule still valid */
    bool quit_workers;            /* Signals worker threads to quit */
    bool in_progress;             /* Is world being progressed */
    bool is_merging;              /* Is world currently being merged */
    bool auto_merge;              /* Are stages auto-merged by ecs_progress */
    bool measure_frame_time;      /* Time spent on each frame */
    bool measure_system_time;     /* Time spent by each system */
    bool should_quit;             /* Did a system signal that app should quit */
    bool should_match;            /* Should tablea be rematched */
    bool should_resolve;          /* If a table reallocd, resolve system refs */
    bool locking_enabled;         /* Lock world when in progress */    
};

#endif
