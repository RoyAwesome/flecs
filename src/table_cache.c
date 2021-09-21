#include "private_api.h"

/* Move table from empty to non-empty list, or vice versa */
static
int32_t move_table(
    ecs_table_cache_t *cache,
    ecs_table_t *table,
    int32_t index,
    ecs_vector_t **dst_array,
    ecs_vector_t *src_array,
    bool empty)
{
    (void)table;

    int32_t new_index = 0, old_index = 0;
    ecs_size_t size = cache->payload_size;
    int32_t last_src_index = ecs_vector_count(src_array) - 1;
    ecs_assert(last_src_index >= 0, ECS_INTERNAL_ERROR, NULL);

    ecs_table_cache_hdr_t *elem = ecs_vector_last_t(src_array, size, 8);
    
    /* The last table of the source array will be moved to the location of the
     * table to move, do some bookkeeping to keep things consistent. */
    if (last_src_index) {
        int32_t *old_index_ptr = ecs_map_get(cache->index, 
            int32_t, elem->table->id);
        ecs_assert(old_index_ptr != NULL, ECS_INTERNAL_ERROR, NULL);

        int32_t old_index = old_index_ptr[0];
        if (!empty) {
            if (old_index >= 0) {
                /* old_index should be negative if not empty, since
                 * we're moving from the empty list to the non-empty list. 
                 * However, if the last table in the source array is also
                 * the table being moved, this can happen. */
                ecs_assert(table == elem->table, ECS_INTERNAL_ERROR, NULL);
                // continue
            } else {
                /* If not empty, src = the empty list, and index should
                 * be negative. */
                old_index = old_index * -1 - 1; /* Normalize */
            }
        }

        if (old_index == last_src_index) {
            old_index_ptr[0] = index;
        }
    } else {
        /* If last_src_index is 0, the table to move was the only table in the
         * src array, so no other administration needs to be updated. */
    }

    /* Actually move the table. Only move from src to dst if we have a
     * dst_array, otherwise just remove it from src. */
    if (dst_array) {
        if (!empty) {
            old_index = index * -1 - 1;
        } else {
            old_index = index;
        }

        new_index = ecs_vector_count(*dst_array);
        ecs_vector_move_index_t(dst_array, src_array, size, 8, old_index);

        /* Make sure table is where we expect it */
        elem = ecs_vector_last_t(*dst_array, size, 8);
        ecs_assert(elem->table == table, ECS_INTERNAL_ERROR, NULL);
        ecs_assert(ecs_vector_count(*dst_array) == (new_index + 1), 
            ECS_INTERNAL_ERROR, NULL);  
    } else {
        ecs_vector_remove_t(src_array, size, 8, old_index);
    }

    /* Ensure that src array has now one element less */
    ecs_assert(ecs_vector_count(src_array) == last_src_index, 
        ECS_INTERNAL_ERROR, NULL);

    if (empty) {
        /* Table is now empty, index is negative */
        new_index = new_index * -1 - 1;
    }

    return new_index;
}

void _ecs_table_cache_init(
    ecs_table_cache_t *cache,
    ecs_size_t size)
{
    ecs_assert(size >= ECS_SIZEOF(ecs_table_cache_hdr_t), 
        ECS_INTERNAL_ERROR, NULL);
    cache->index = ecs_map_new(int32_t, 0);
    cache->empty_tables = NULL;
    cache->tables = NULL;
    cache->payload_size = size;
}

void ecs_table_cache_fini(
    ecs_table_cache_t *cache)
{
    ecs_map_free(cache->index);
    ecs_vector_free(cache->empty_tables);
    ecs_vector_free(cache->tables);
}

void* _ecs_table_cache_insert(
    ecs_table_cache_t *cache,
    const ecs_table_t *table,
    ecs_size_t size)
{
    int32_t index;
    ecs_table_cache_hdr_t *result;
    bool empty;

    if (!table) {
        empty = false;
    } else {
        empty = ecs_table_count(table) == 0;
    }

    if (empty) {
        result = ecs_vector_add_t(&cache->empty_tables, size, 8);
        index = -ecs_vector_count(cache->empty_tables);
    } else {
        index = ecs_vector_count(cache->tables);
        result = ecs_vector_add_t(&cache->tables, size, 8);
    }

    if (table) {
        ecs_map_set(cache->index, table->id, &index);
    }

    result->table = (ecs_table_t*)table;

    return result;
}

void _ecs_table_cache_remove(
    ecs_table_cache_t *cache,
    ecs_table_t *table)
{
    int32_t *index = ecs_map_get(cache->index, int32_t, table->id);
    if (!index) {
        return;
    }

    if (index[0] < 0) {
        move_table(cache, table, index[0], NULL, cache->empty_tables, false);
    } else {
        move_table(cache, table, index[0], NULL, cache->tables, true);
    }
}

void _ecs_table_cache_set_empty(
    ecs_table_cache_t *cache,
    ecs_table_t *table,
    bool empty)
{
    int32_t *index = ecs_map_get(cache->index, int32_t, table->id);
    if (!index) {
        return;
    }

    /* If table is already in the correct array nothing needs to be done */
    if (empty && index[0] < 0) {
        return;
    } else if (!empty && index[0] >= 0) {
        return;
    }

    if (index[0] < 0) {
        index[0] = move_table(
            cache, table, index[0], &cache->tables, cache->empty_tables, empty);
    } else {
        index[0] = move_table(
            cache, table, index[0], &cache->empty_tables, cache->tables, empty);
    }
}