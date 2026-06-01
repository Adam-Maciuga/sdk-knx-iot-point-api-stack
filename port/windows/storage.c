/* 
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */

#include <direct.h>
#include <io.h>
#include "oc_config.h"
#include "port/oc_storage.h"
#include "port/oc_log.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>

#define STORE_PATH_SIZE 64

static char store_path[STORE_PATH_SIZE];
static size_t store_path_len;
static bool path_set = false;

int oc_storage_config(const char* store)
{
    if (!store || !*store) 
    {
        return -EINVAL;
    }

    store_path_len = strlen(store);
    if (store_path_len >= STORE_PATH_SIZE) 
    {
        return -ENOENT;
    }

    strncpy(store_path, store, store_path_len);

    if (store_path[store_path_len - 1] != '/' && store_path[store_path_len - 1] != '\\') 
    {
        ++store_path_len;
        if (store_path_len >= STORE_PATH_SIZE) 
        {
            return -ENOENT;
        }

        store_path[store_path_len - 1] = '/';
    }

    path_set = true;
    char temp_dir[STORE_PATH_SIZE]; 
    strcpy(temp_dir, store);

    // remove leading "./" and trailing "/" from store path
    if (store_path_len > 2 && store[0] == '.' && store[1] == '/') 
    {
        strcpy(temp_dir, &store[2]);
    }

    const size_t dir_len = strlen(temp_dir);
    if (temp_dir[dir_len - 1] == '/') 
    { // remove trailing "/"
        temp_dir[dir_len - 1] = '\0';
    }

    OC_INF("Creating storage directory at %s", temp_dir);
    #ifdef __GNUC__
    int ret_val = mkdir(temp_dir);
    #else
    const int ret_val = _mkdir(temp_dir);
    #endif
    OC_DBG("Result (0:ok; -1:EEXIST or ENOENT (path not found)) : %d", ret_val);

    return ret_val;
}

long oc_storage_read(const char* store, uint8_t* buf, size_t size)
{
    const size_t store_len = strlen(store);

    if (!path_set || (store_len + store_path_len >= STORE_PATH_SIZE)) {
        return -ENOENT;
    }

    strncpy(store_path + store_path_len, store, store_len);
    store_path[store_path_len + store_len] = '\0';

    FILE* fp = fopen(store_path, "rb");
    if (!fp) 
    {
        OC_ERR("Error on open file: %s", store_path);
        return -EINVAL;
    }

    size = fread(buf, 1, size, fp);
    (void) fclose(fp);

    return (long) size;
}

long oc_storage_write(const char* store, uint8_t* buf, size_t size)
{
    const size_t store_len = strlen(store);

    if (!path_set || (store_len + store_path_len >= STORE_PATH_SIZE)) 
    {
        return -ENOENT;
    }

    strncpy(store_path + store_path_len, store, store_len);
    store_path[store_path_len + store_len] = '\0';

    FILE* fp = fopen(store_path, "wb");
    if (!fp) 
    {
        OC_ERR("Error on open file: %s", store_path);	
        return -EINVAL;
    }

    size = fwrite(buf, 1, size, fp);
    (void) fflush(fp);
    (void) _commit(_fileno(fp));
    (void) fclose(fp);

    return (long) size;
}

int oc_storage_erase(const char* store)
{
    const size_t store_len = strlen(store);

    if (!path_set || store_len + store_path_len >= STORE_PATH_SIZE)
    {
        return -ENOENT;
    }

    store_path[store_path_len] = '/';
    strncpy(store_path + store_path_len + 1, store, store_len);
    store_path[1 + store_path_len + store_len] = '\0';

    return remove(store_path);
}

