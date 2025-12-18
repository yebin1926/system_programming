/*--------------------------------------------------------------------*/
/* hashtable.c                                                        */
/* Author: Junghan Yoon, KyoungSoo Park                               */
/* Modified by: Yebin Pyun                                      */
/*--------------------------------------------------------------------*/
#include "hashtable.h"
#include <string.h>
/*--------------------------------------------------------------------*/

int hash(const char *key, size_t hash_size) //converts a key string into a bucket index
{
    TRACE_PRINT();
    unsigned int hash = 0;
    while (*key) //loop over character until reaching null '\n'
    {
        hash = (hash << 5) + *key++; //shift left by 5 bits (multiply by 32), then add key
        //move to the next char in key 
    }

    return hash % hash_size; //return bucket index
}
/*--------------------------------------------------------------------*/
hashtable_t *hash_init(size_t hash_size, int delay)
{
    TRACE_PRINT();
    int i, j, ret;
    hashtable_t *table = calloc(1, sizeof(hashtable_t));

    if (table == NULL)
    {
        DEBUG_PRINT("Failed to allocate memory for hash table");
        return NULL;
    }

    table->hash_size = hash_size;

    table->buckets = calloc(hash_size, sizeof(node_t *));
    if (table->buckets == NULL)
    {
        DEBUG_PRINT("Failed to allocate memory for hash table buckets");
        free(table);
        return NULL;
    }

    table->locks = calloc(hash_size, sizeof(rwlock_t));
    if (table->locks == NULL)
    {
        DEBUG_PRINT("Failed to allocate memory for hash table locks");
        free(table->buckets);
        free(table);
        return NULL;
    }

    table->bucket_sizes = calloc(hash_size, sizeof(*table->bucket_sizes));
    if (table->bucket_sizes == NULL)
    {
        DEBUG_PRINT("Failed to allocate memory for hash table "
                    "bucket sizes");
        free(table->buckets);
        free(table->locks);
        free(table);
        return NULL;
    }

    for (i = 0; i < hash_size; i++)
    {
        table->buckets[i] = NULL;
        table->bucket_sizes[i] = 0;
        ret = rwlock_init(&table->locks[i], delay);
        if (ret != 0)
        {
            DEBUG_PRINT("Failed to initialize read-write lock");
            for (j = 0; j < i; j++)
            {
                rwlock_destroy(&table->locks[j]);
            }
            free(table->buckets);
            free(table->locks);
            free(table->bucket_sizes);
            free(table);
            return NULL;
        }
    }

    return table;
}
/*--------------------------------------------------------------------*/
int hash_destroy(hashtable_t *table)
{
    TRACE_PRINT();
    node_t *node, *tmp;
    int i;

    for (i = 0; i < table->hash_size; i++)
    {
        node = table->buckets[i];
        while (node)
        {
            tmp = node;
            node = node->next;
            free(tmp->key);
            free(tmp->value);
            free(tmp);
        }
        if (rwlock_destroy(&table->locks[i]) != 0)
        {
            DEBUG_PRINT("Failed to destroy read-write lock");
            return -1;
        }
    }

    free(table->buckets);
    free(table->locks);
    free(table->bucket_sizes);
    free(table);

    return 0;
}
/*--------------------------------------------------------------------*/
int hash_insert(hashtable_t *table, const char *key, const char *value)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    //TODO: validate inputs
    if(table == NULL || key == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }

    //TODO: compute which bucket to use
    int idx = hash(key, table->hash_size);

    //TODO: Acquire bucket's write lock
    rwlock_t *rw = &table->locks[idx];
    if(rwlock_write_lock(rw) < 0) return -1;

    //TODO: check for collision (if key alr exists)
    node_t *next_node = table->buckets[idx];
    while(next_node != NULL){
        if(strcmp(key, next_node->key) == 0){
            rwlock_write_unlock(rw);
            return 0;
        }
        next_node = next_node->next;
    }

    //TODO: Allocate and initialise new node
    node_t *newnode = malloc(sizeof(node_t));
    if(newnode == NULL){
        errno = ENOMEM;
        rwlock_write_unlock(rw);
        return -1;
    }

    newnode->key = strdup(key);
    newnode->value = strdup(value);
    if(newnode->key == NULL || newnode->value == NULL){
        errno = ENOMEM;
        rwlock_write_unlock(rw);
        free(newnode->key);
        free(newnode->value);
        free(newnode);
        return -1;
    }
    newnode->key_size = strlen(key)+1;
    newnode->value_size = strlen(value)+1;
    newnode->next = NULL;

    //TODO: Link it to the head of bucket
    newnode->next = table->buckets[idx];
    table->buckets[idx] = newnode;
    table->bucket_sizes[idx]++;

    //TODO: Releast lock and return success
    rwlock_write_unlock(rw);

/*--------------------------------------------------------------------*/
    return 1;
}
/*--------------------------------------------------------------------*/
int hash_read(hashtable_t *table, const char *key, char *dst, int quick)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    //TODO: validation checks
    if(table == NULL || key == NULL || dst == NULL){
        errno = EINVAL;
        return -1;
    }
    
    //TODO: Compute bucket and acquire lock
    int idx = hash(key, table->hash_size);
    rwlock_t *rw = &table->locks[idx];
    if(rwlock_read_lock(rw, quick) < 0) return -1;

    //TODO: traverse through table to find key value pair
    node_t *next_node = table->buckets[idx];
    while(next_node != NULL){
        if(strcmp(next_node->key, key) == 0){
            strncpy(dst, next_node->value, next_node->value_size);
            rwlock_read_unlock(rw);
            return 1;
        }
        next_node = next_node->next;
    }

    //Release lock
    rwlock_read_unlock(rw);

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int hash_update(hashtable_t *table, const char *key, const char *value)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    //TODO: Validation checks
    if(table == NULL || key == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }

    //TODO: Compute bucket and Acquire lock
    int idx = hash(key, table->hash_size);
    rwlock_t *rw = &table->locks[idx];
    if(rwlock_write_lock(rw) < 0) {
        return -1;
    }

    //TODO: traverse through bucket and update value
    node_t *next_node = table->buckets[idx];
    while(next_node != NULL){
        if(strcmp(next_node->key, key) == 0){
            char *temp = strdup(value);
            if(temp == NULL){ //failure in strdup
                errno = ENOMEM;
                rwlock_write_unlock(rw);
                return -1;
            }
            char *old_value = next_node->value;
            next_node->value = temp;
            free(old_value);
            next_node->value_size = strlen(value)+1;
            rwlock_write_unlock(rw);
            return 1;
        }
        next_node = next_node->next;
    }

    //TODO: release lock and return
    rwlock_write_unlock(rw);
/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int hash_delete(hashtable_t *table, const char *key)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    if(table == NULL || key == NULL) {
        errno = EINVAL;
        return -1;
    }

    //TODO: Compute bucket and Acquire lock
    int idx = hash(key, table->hash_size);
    rwlock_t *rw = &table->locks[idx];
    if(rwlock_write_lock(rw) < 0) {
        return -1;
    }

    //TODO: traverse through bucket and find node to delete
    node_t *curr_node = table->buckets[idx];
    node_t *prev_node = NULL;
    while(curr_node != NULL){
        //if found, delete and change node links and return
        if(strcmp(curr_node->key, key) == 0){
            if(prev_node == NULL){ //if it's the first node
                table->buckets[idx] = curr_node->next;
            } else {
                prev_node->next = curr_node->next;
            }
            free(curr_node->key);
            free(curr_node->value);
            free(curr_node);
            table->bucket_sizes[idx]--;
            rwlock_write_unlock(rw);
            return 1;
        }
        prev_node = curr_node;
        curr_node = curr_node->next;
    }

    //TODO: release lock and return
    rwlock_write_unlock(rw);

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
/**
 * function to dump the contents of the hash table,
 * including locks status
 */
void hash_dump(hashtable_t *table)
{
    TRACE_PRINT();
    node_t *node;
    int i;
    size_t total_entries = 0;

    printf("[Hash Table Dump]");
    for (i = 0; i < table->hash_size; i++)
    {
        total_entries += table->bucket_sizes[i];
    }
    printf("Total Entries: %ld\n", total_entries);

    for (i = 0; i < table->hash_size; i++)
    {
        if (!table->bucket_sizes[i])
        {
            continue;
        }
        printf("Bucket %d: %ld entries\n", i, table->bucket_sizes[i]);
        printf("  Lock State -> Read Count: %d, Write Count: %d\n",
               rwlock_current_readers(&table->locks[i]),
               rwlock_current_writers(&table->locks[i]));
        node = table->buckets[i];
        while (node)
        {
            printf("    K/V: %s / %s\n", node->key, node->value);
            node = node->next;
        }
    }
    printf("End of Dump\n");
}
/*--------------------------------------------------------------------*/