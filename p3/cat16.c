/*
Student: Kevius Tribble
Instructor: Jacob Sorber
CPSC 3020
11/9/2025
*/

#include "cat16.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define filename_bytes 8
#define extension_bytes 3
#define FOUR_MB 0x400000
#define ROOT_DIR_START_SECTOR 65 // 65*512
#define ROOT_DIR_END_SECTOR 97 // 97*512
#define SECTOR_SIZE 512
#define SECTORS_PER_CLUSTER 1
#define SECTORS_PER_FAT 32
#define TOTAL_SECTORS 0x2000
#define path_depth 99 // maximum depth of a path 
#define max_path_length 9999
#define ENTRY_SIZE 32
#define MAX_DIR_ENTRIES 9999
#define max_number_of_tokens 99999

struct cat16_volume{
    long image_size;
    uint16_t sector_size; // 2 byte int
    uint8_t sectors_per_cluster; // 1 byte int
    uint16_t sectors_per_fat;
    uint8_t number_fats;
    uint16_t total_sectors;
    FILE *image_buffer;
    int last_error_code; 
};

typedef struct cat16_dir_entry cat16_dir_entry_t;

struct cat16_dir_entry{
    char filename[filename_bytes]; // add the null terminator 
    char extension[extension_bytes];
    uint8_t attributes;
    uint16_t reserved;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t hidden_bytes; // in the table there is a mysterious extra 2 bytes between these two fields
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_logical_cluster;
    uint32_t file_size;
    int byte_offset; // entries location in the image buffer
    int entry_found; // 1 if found 0 if not found
    bool deleted;
    char disk_name[filename_bytes + extension_bytes + 2]; // 8 + 3 + . + null terminator
    int start_sector; 
    int end_sector;
    uint16_t parent_cluster; // parent cluster. 
};

// my helper functions 

//read a clusters value from the fat
static uint16_t read_fat_entry(cat16_volume_t *volume, uint16_t cluster){
    uint16_t value;
    fseek(volume->image_buffer, (SECTOR_SIZE * 1) + (cluster * sizeof(cluster)), SEEK_SET); // FAT1 STARTS AT SECTOR 1
    fread(&value, sizeof(value), 1, volume->image_buffer);
    return value;
}

//next clsuter in a directories fat chain
static uint16_t next_dir_cluster(cat16_volume_t *volume, uint16_t cluster){
    uint16_t next_cluster = read_fat_entry(volume, cluster);
    if (next_cluster >= 0x0002 && next_cluster <= 0xFFEF){
        return next_cluster;
    }
    return 0;
}


// move through the fat looking for a free cluster. returns the cluster number of the first free cluster found.
static uint16_t find_free_cluster(cat16_volume_t *volume){
    for (uint16_t cluster = 0x0002; cluster < 0xFFEF; cluster++){
        uint16_t value = read_fat_entry(volume, cluster); // raw value. next_dir_cluster would also return 0 for 0xFFFF
        if (value == 0x0000){
            return cluster;
        }
    }
    return 0xFFFF; // no free clusters found | out of space
}

//zero out a cluster in the data region.
static void zero_out_cluster(cat16_volume_t *volume, uint16_t cluster){
    fseek(volume->image_buffer, (97 + (cluster - 2)) * SECTOR_SIZE, SEEK_SET);
    uint8_t zero_buffer[SECTOR_SIZE] = {0};
    fwrite(zero_buffer, sizeof(zero_buffer), 1, volume->image_buffer);
}

//write a cluster number to the fat table at the specified cluster number.
static void write_fat_entry(cat16_volume_t *volume, uint16_t cluster, uint16_t value){
    fseek(volume->image_buffer, (SECTOR_SIZE * 1) + (cluster * sizeof(cluster)), SEEK_SET); // FAT1 STARTS AT SECTOR 1
    fwrite(&value, sizeof(value), 1, volume->image_buffer);
    fseek(volume->image_buffer, (SECTOR_SIZE * 33) + (cluster * sizeof(cluster)), SEEK_SET); // FAT2 STARTS AT SECTOR 33
    fwrite(&value, sizeof(value), 1, volume->image_buffer);
}


//add a cluster to the end of a directory's fat chain.
static uint16_t grow_directory(cat16_volume_t *volume, uint16_t cluster){
    if (cluster == 0x0000){
        return 0xFFFF; //cant grow root directory
    }

    //go to last cluster in the directories fat chain
    uint16_t last_cluster = cluster;
    uint16_t next_cluster = next_dir_cluster(volume, last_cluster);
    while (next_cluster !=0x0000){
        last_cluster = next_cluster;
        next_cluster = next_dir_cluster(volume, last_cluster);
    }
    // find a free cluster to add to the end of the chain
    uint16_t free_cluster = find_free_cluster(volume);
    if (free_cluster == 0xFFFF){
        return 0xFFFF; // no free clusters found | out of space
    }
    // write the free cluster to the end of the directories fat chain
    write_fat_entry(volume, last_cluster, free_cluster);
    write_fat_entry(volume, free_cluster, 0xFFFF); //end
    // zero out the new cluster
    zero_out_cluster(volume, free_cluster);
    return free_cluster;
}

// search directory for a file or directory with the specified filename.
// returns a struct representing the directory entry if found, else empty entry with entry_found = 0 
static cat16_dir_entry_t search_directory(cat16_volume_t *volume, const char *filename, const char *extension, uint16_t dir_cluster){
    cat16_dir_entry_t entry;
    entry.entry_found = 0; // initialize entry_found to 0
    entry.parent_cluster = dir_cluster;
    uint16_t cluster = dir_cluster;
    int start_sector = ROOT_DIR_START_SECTOR;
    int end_sector = ROOT_DIR_END_SECTOR;
    if (dir_cluster != 0x0000){ // subdirectory. one sector per cluster
        start_sector = 97 + (dir_cluster - 2);
        end_sector = start_sector + 1;
    }
    while (true){
        for (int i = start_sector; i < end_sector; i++){
            for (int j  = 0; j < SECTOR_SIZE / ENTRY_SIZE; j++){
                fseek(volume->image_buffer, (i * SECTOR_SIZE) + (j * ENTRY_SIZE), SEEK_SET);
                fread(entry.filename, sizeof(entry.filename), 1, volume->image_buffer);
                fread(entry.extension, sizeof(entry.extension), 1, volume->image_buffer);
                fread(&entry.attributes, sizeof(entry.attributes), 1, volume->image_buffer);
                fread(&entry.reserved, sizeof(entry.reserved), 1, volume->image_buffer);
                fread(&entry.create_time, sizeof(entry.create_time), 1, volume->image_buffer);
                fread(&entry.create_date, sizeof(entry.create_date), 1, volume->image_buffer);
                fread(&entry.last_access_date, sizeof(entry.last_access_date), 1, volume->image_buffer);
                fread(&entry.last_write_time, sizeof(entry.last_write_time), 1, volume->image_buffer);
                fread(&entry.hidden_bytes, sizeof(entry.hidden_bytes), 1, volume->image_buffer);
                fread(&entry.last_write_date, sizeof(entry.last_write_date), 1, volume->image_buffer);
                fread(&entry.first_logical_cluster, sizeof(entry.first_logical_cluster), 1, volume->image_buffer);
                fread(&entry.file_size, sizeof(entry.file_size), 1, volume->image_buffer);
                entry.byte_offset = (i * SECTOR_SIZE) + (j * ENTRY_SIZE);
                if ((unsigned char)entry.filename[0] == 0xE5){
                    entry.deleted = true;
                }
                else{
                    entry.deleted = false;
                }
                // deleted and unused entries can't match
                if (entry.deleted || (unsigned char)entry.filename[0] == 0x00){
                    continue;
                }
                //must use memcmp since file fname and extension are not null terminated. 
                if (memcmp(entry.filename, filename, 8) == 0 && memcmp(entry.extension, extension, 3) == 0){
                    entry.entry_found = 1;
                    return entry;
                }
            }
        }
        // searched root and didnt find it
        if (cluster == 0x0000){
            return entry;
        }
        // in a directory follow fat chain if if its zero then we didnt find it 
        cluster = next_dir_cluster(volume, cluster);
        if (cluster == 0x0000){
            return entry; // no more clusters in the chain
        }
        //next sector(s) to search
        start_sector = 97 + (cluster - 2);
        end_sector = start_sector + 1;
    }
}

// check all 16 entries availible in a sector and store them in the dir_entries array.
// skip '.' and '..' entries in subdirectories. and unused entries.
static void scan_sector(cat16_volume_t *volume, int sector, cat16_dirent_t *dir_entries, size_t *entries_read){
    for (int j  = 0; j < SECTOR_SIZE / ENTRY_SIZE; j++){
        cat16_dir_entry_t entry;
        // seek the entry on the volume
        fseek(volume->image_buffer, (sector * SECTOR_SIZE) + (j * ENTRY_SIZE), SEEK_SET);
        fread(entry.filename, sizeof(entry.filename), 1, volume->image_buffer);
        fread(entry.extension, sizeof(entry.extension), 1, volume->image_buffer);
        fread(&entry.attributes, sizeof(entry.attributes), 1, volume->image_buffer);
        fread(&entry.reserved, sizeof(entry.reserved), 1, volume->image_buffer);
        fread(&entry.create_time, sizeof(entry.create_time), 1, volume->image_buffer);
        fread(&entry.create_date, sizeof(entry.create_date), 1, volume->image_buffer);
        fread(&entry.last_access_date, sizeof(entry.last_access_date), 1, volume->image_buffer);
        fread(&entry.last_write_time, sizeof(entry.last_write_time), 1, volume->image_buffer);
        fread(&entry.hidden_bytes, sizeof(entry.hidden_bytes), 1, volume->image_buffer);
        fread(&entry.last_write_date, sizeof(entry.last_write_date), 1, volume->image_buffer);
        fread(&entry.first_logical_cluster, sizeof(entry.first_logical_cluster), 1, volume->image_buffer);
        fread(&entry.file_size, sizeof(entry.file_size), 1, volume->image_buffer);
        entry.byte_offset = (sector * SECTOR_SIZE) + (j * ENTRY_SIZE); // what byte the entry starts at
        // check if the entry is deleted or unused.
        if ((unsigned char)entry.filename[0] == 0xE5 || (unsigned char)entry.filename[0] == 0x00){
            continue; // skip deleted or unused entries
        }
        // Disk stores name with padding ex: A.txt -> 'A       .txt'
        char name_on_disk[13];
        int len = 0; // need external tracker since file name and entry are seperate
        for (int k = 0; k < 8; k++){
            if (entry.filename[k] != ' '){
                name_on_disk[len] = entry.filename[k];
                len++;
            }
        }
        // if an extesion exists add a '.' between name and extension
        if (entry.extension[0] != ' '){
            name_on_disk[len] = '.';
            len++;
            for (int k = 0; k < 3; k++){
                if (entry.extension[k] != ' '){
                    name_on_disk[len] = entry.extension[k];
                    len++;
                }
            }
        }
        name_on_disk[len] = '\0';
        strcpy(entry.disk_name, name_on_disk); // save name on disk to entry struct | in case I need it later
        // skip '.' and '..' entries in subdirectories.
        if (strcmp(name_on_disk, ".") == 0 || strcmp(name_on_disk, "..") == 0){
            continue;
        }
        // fill in the dir_entries array with the entry information
        cat16_dirent_t dirent;
        strncpy(dirent.name, name_on_disk, sizeof(dirent.name));
        dirent.attributes = entry.attributes;
        dirent.size = entry.file_size;
        // add the entry to the dir_entries array
        dir_entries[*entries_read] = dirent;
        (*entries_read)++;
    }
}

// walks through the provided path and searches for each file or directory in the path
// first search through root directory then just follow the first logical clusters. 
static int path_handler(cat16_volume_t *volume, const char *path, cat16_dir_entry_t *entry, int write_allowed, char *filename_out, char *extension_out){
    int current_cluster = 0x0000; // start at root directory
    entry->entry_found = 0; // initialize entry_found to 0

    // HANDLE THE PATH
    char path_copy[max_number_of_tokens]; // copy since strtok modifies the string and path is const
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    // using rentrant version since i'll probably need it for project 4. 
    char *saveptr; // saves str ptr location between calls to strtok_r
    char *tokens = strtok_r(path_copy, "/", &saveptr); //split on / so Dir1/Dir2/File.txt -> Dir1 Dir2 File.txt
    char *token_list[path_depth]; // store the tokens in an array
    int token_count = 0;
    while (tokens != NULL && token_count < path_depth){
        token_list[token_count] = tokens;
        token_count++;
        tokens = strtok_r(NULL, "/", &saveptr);
    }
    // more tokens than the max depth of a path.
    if (tokens != NULL){
        return -ENAMETOOLONG;
    }
    for (int i = 0; i < token_count; i++){
        char *path_ptr = token_list[i];
        char filename_internal[9]; // 8 + null terminator
        char extension_internal[4]; // 3 + null terminator
        char *dot = strrchr(path_ptr, '.'); // get the last . in the token
        if (dot != NULL){
            int filename_len = dot - path_ptr;
            int extension_len = strlen(dot + 1);
            if (filename_len > 8 || extension_len > 3){
                return -ENAMETOOLONG;
            }
            snprintf(filename_internal, sizeof(filename_internal), "%-8.*s", filename_len, path_ptr); // only the part before the dot, padded to 8
            snprintf(extension_internal, sizeof(extension_internal), "%-3s", dot + 1); // pad the extension to 3
        }
        else{
            if (strlen(path_ptr) > 8){
                return -ENAMETOOLONG;
            }
            snprintf(filename_internal, sizeof(filename_internal), "%-8s", path_ptr);
            snprintf(extension_internal, sizeof(extension_internal), "%-3s", "");
        }
        // PATH HANDLING END 

        // search for the entry in the current directory
        *entry = search_directory(volume, filename_internal, extension_internal, current_cluster);
        // no dir/file found. only ok if we're creating the last component
        if (entry->entry_found == 0 && (write_allowed == 0 || i != token_count - 1)){ 
            return -ENOENT;
        }
        // end of path reached
        if (i == token_count - 1){
            if (filename_out != NULL && extension_out != NULL){
                strncpy(filename_out, filename_internal, sizeof(filename_internal) - 1 );
                strncpy(extension_out, extension_internal, sizeof(extension_internal)- 1); // excluding the null terminator
            }
            return 0; 
        }
        // update move to first logical cluster of a directory. 
        if (!(entry->attributes & 0x10)){ 
            return -ENOTDIR;
        }
        current_cluster = entry->first_logical_cluster;
    }
    return -ENOENT; // empty path
}

// write a dir entry to the specified dir. if the dir is full, grow it and try again.
static int write_dir_entry(cat16_volume_t *volume, cat16_dir_entry_t *entry, uint8_t attributes, uint16_t first_logical_cluster, uint32_t bytes_to_write){
    // 0 out unused fields
    entry->attributes = attributes; 
    entry->reserved = 0;
    entry->create_time = 0;
    entry->create_date = 0;
    entry->last_access_date = 0;
    entry->hidden_bytes = 0;
    entry->last_write_time = 0;
    entry->last_write_date = 0;
    entry->first_logical_cluster = first_logical_cluster;
    entry->file_size = bytes_to_write;
    // the DIRECTORY we are writing into is entry->parent_cluster. (first_logical_cluster is what the new entry points to)
    uint16_t cluster = entry->parent_cluster;
    int start_sector = ROOT_DIR_START_SECTOR;
    int end_sector = ROOT_DIR_END_SECTOR;
    if (cluster != 0x0000){ // subdirectory
        start_sector = 97 + (cluster - 2);
        end_sector = start_sector + 1;
    }
    while (true){
        // find a  free entry in bewtween the start and end sectors of the dir. 
        for (int i = start_sector; i < end_sector; i++){
            for (int j = 0; j < SECTOR_SIZE / ENTRY_SIZE; j++){
                fseek(volume->image_buffer, (i * SECTOR_SIZE) + (j * ENTRY_SIZE), SEEK_SET);
                uint8_t first_byte;
                fread(&first_byte, sizeof(first_byte), 1, volume->image_buffer);
                if (first_byte == 0x00 || first_byte == 0xE5){ // free entry found
                    fseek(volume->image_buffer, (i * SECTOR_SIZE) + (j * ENTRY_SIZE), SEEK_SET);
                    fwrite(entry->filename, sizeof(entry->filename), 1, volume->image_buffer);
                    fwrite(entry->extension, sizeof(entry->extension), 1, volume->image_buffer);
                    fwrite(&entry->attributes, sizeof(entry->attributes), 1, volume->image_buffer);
                    fwrite(&entry->reserved, sizeof(entry->reserved), 1, volume->image_buffer);
                    fwrite(&entry->create_time, sizeof(entry->create_time), 1, volume->image_buffer);
                    fwrite(&entry->create_date, sizeof(entry->create_date), 1, volume->image_buffer);
                    fwrite(&entry->last_access_date, sizeof(entry->last_access_date), 1, volume->image_buffer);
                    fwrite(&entry->hidden_bytes, sizeof(entry->hidden_bytes), 1, volume->image_buffer);
                    fwrite(&entry->last_write_time, sizeof(entry->last_write_time), 1, volume->image_buffer);
                    fwrite(&entry->last_write_date, sizeof(entry->last_write_date), 1, volume->image_buffer);
                    fwrite(&entry->first_logical_cluster, sizeof(entry->first_logical_cluster), 1, volume->image_buffer);
                    fwrite(&entry->file_size, sizeof(entry->file_size), 1, volume->image_buffer);
                    return 0; // success
                }
            }
        }
        // no space in root 
        if (cluster == 0x0000){
            return 1; // root dir is full. 
        }
        // no space in subdirectory. grow the directory and try again.
        cluster = next_dir_cluster(volume, cluster);
        if (cluster == 0x0000){
            return 1; // no more clusters in the chain.
        }
        // next sector(s) to search
        start_sector = 97 + (cluster - 2);
        end_sector = start_sector + 1;
    }
}


// add a directory entry to the specified dir. if the dir is full, grow it and try again.
static int add_dir_entry(cat16_volume_t *volume, cat16_dir_entry_t *entry, uint8_t attributes, uint16_t first_logical_cluster, uint32_t bytes_to_write){
    int result = write_dir_entry(volume, entry, attributes, first_logical_cluster, bytes_to_write);
    if (result == 1){
        if (grow_directory(volume, entry->parent_cluster) == 0xFFFF){
            return -ENOSPC;
        }
        result = write_dir_entry(volume, entry, attributes, first_logical_cluster, bytes_to_write);
        if (result == 1){
            return -ENOSPC; // no free entry found after growing directory
        }
    }
    return 0; // success
}

// the error handling was easy to do. 
/*
 * cat16_vstrerror -- returns the error message corresponding
 *      to the last function failure. For example, if listdir
 *      just returned NULL, then this function would tell us
 *      why. Error codes should be stored on a per-volume
 *      basis. This function's behavior is undefined after
 *      a successful cat16 function return.
 * Return:
 *  - On success, it should return an error message
 *      corresponding to the last cat16_ function failure.
 *      You should use the standard error messages (returned
 *      by strerror() for the error codes specified in the
 *      project description. For any other error codes you
 *      choose to support, feel free to use an error message
 *      of your choosing.
 *  - On failure, it should return NULL. The only time it
 *      would do that is if volume is NULL.
 */
const char *cat16_vstrerror(cat16_volume_t *volume){
    if (volume == NULL){
        return NULL;
    }
    return cat16_strerror(volume->last_error_code);
}

/*
 * cat16_vstrerror -- returns the error message corresponding
 *      to the error_code, returned from a recent cat16_
 *      function failure. For example, if mkdir returned
 *      a negative error code, then this function would
 *      tell us what that code means. Note that since these
 *      messages are read only, they can be string constants.
 *      They do not need to be allocated on the heap. In
 *      fact, it's best if they are not, because I will not
 *      free them.
 * Return:
 *  - This function should always return an error message
 *      (including for success (0) and error_codes you don't
 *      recognize).
 *      If I pass in an error code you don't recognize, then
 *      you should still return a default message. You should
 *      use the standard error messages (returned by
 *      strerror() for the error codes specified in the
 *      project description. For any other error codes you
 *      choose to support, feel free to use an error message
 *      of your choosing.
 */
const char *cat16_strerror(int error_code){
    switch (error_code){
    case 0:
        return "Success";
    case -EINVAL:
        return strerror(EINVAL);
    case -ENAMETOOLONG:
        return strerror(ENAMETOOLONG);
    case -ENOENT:
        return strerror(ENOENT);
    case -ENOTDIR:
        return strerror(ENOTDIR);
    case -ENOTEMPTY:
        return strerror(ENOTEMPTY);
    case -EISDIR:
        return strerror(EISDIR);
    case -ENOSPC:
        return strerror(ENOSPC);
    case -EEXIST:
        return strerror(EEXIST);
    default:
        return "no clue what you did tbh";
    }
}


/*
 * cat16_mount -- load's a FAT16 disk image into memory.
 * Return:
 *   - NULL: on failure if the image could not be
 *      mounted --- file not found, image doesn't follow
 *      the expected format (image size = 4mb, sectorsize = 512,
 *      sectors per cluster = 1, sectors per fat = 32, number of FATs = 2
 *      total_sectors = 0x2000).
 *   - Pointer to an opaque struct representing the mounted
 *      disk image. You can store whatever you need to store
 *      in this struct.
 */
cat16_volume_t *cat16_mount(const char *image_path){
    cat16_volume_t *volume = malloc(sizeof(cat16_volume_t));
    FILE *file = fopen(image_path, "rb+");
    // image could not be mounted | no such file or directory
    if (file == NULL){
        volume->last_error_code = ENOENT;
        free(volume);
        return NULL;
    }
    // determine the size of the image 
    fseek(file, 0, SEEK_END);
    volume->image_size = ftell(file); // current possiton of pointer in the stream. since I'm at the end of the file, this will give us the size of the filev
    // store the image buffer in the volume struct
    volume->image_buffer = file;
    // fseek moves file pointer to specified position in the file. 
    // SEEK_SET goes to the absolute position in the file. 
    // fread (where to store data, size of the data, number of times to read data, where its stored) | reads raw bytes data from a file and sends to a buffer. 
    // jump around to fill in the struct with the correct values
    fseek(file, 11, SEEK_SET);
    fread(&volume->sector_size, sizeof(volume->sector_size), 1, file);
    fseek(file, 13, SEEK_SET);
    fread(&volume->sectors_per_cluster, sizeof(volume->sectors_per_cluster), 1, file);
    fseek(file, 22, SEEK_SET);
    fread(&volume->sectors_per_fat, sizeof(volume->sectors_per_fat), 1, file);
    fseek(file, 16, SEEK_SET);
    fread(&volume->number_fats, sizeof(volume->number_fats), 1, file);
    fseek(file, 19, SEEK_SET);
    fread(&volume->total_sectors, sizeof(volume->total_sectors), 1, file);
    // check that the image meets the expected format
    if (volume->image_size != FOUR_MB || volume->sector_size != SECTOR_SIZE || volume->sectors_per_cluster != SECTORS_PER_CLUSTER || volume->sectors_per_fat != SECTORS_PER_FAT || volume->number_fats != 2 || volume->total_sectors != TOTAL_SECTORS){
        fclose(file);
        free(volume);
        return NULL;
    }
    return volume;
}

/*
 * cat16_unmount -- unmounts a volume object previously
 *      returned from cat16_mount(). It should close any open
 *      file descriptors and free any memory associated with
 *      the mounted volume, including the memory for the
 *      volume struct.
 * Return:
 *  - 0 (zero) on success
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code into the
 *      cat16_strerror() function, in order to get the
 *      corresponding error message. The standard system
 *      error message should be used for EINVAL.
 */
int cat16_unmount(cat16_volume_t *volume){
    // invaild argument
    if (volume == NULL){
        return -EINVAL;
    }
    fclose(volume->image_buffer);
    free(volume);
    return 0;
}


/*
 * cat16_readfile -- read a file with the specified name
 *      (path) from the mounted volume.
 *      (bytes_to_write) and content (filedata) specified.
 *      If a file with that name already exists, it should be
 *      overwritten, and the old file data removed.
 * Return:
 *  - On success, it should return a pointer to the bytes
 *      read from the file (the file's contents) in a block
 *      of memory allocated on the heap. You should also
 *      store the number of bytes read in the memory pointed
 *      to by the "bytes_read" argument.
 *
 *  - On failure, it should return NULL, and you should store
 *      a negative error code in the volume somewhere, so
 *      that I can get the error message by calling
 *      cat16_vstrerror(). The standard system error message
 *      should be used for EINVAL, ENAMETOOLONG,
 *      ENOENT, EISDIR, and ENOTDIR.
 */
uint8_t *cat16_readfile(cat16_volume_t *volume, const char *path, size_t *bytes_read){
    // check for invalid args 
    if (volume == NULL){
        return NULL;
    }
    if (path == NULL || bytes_read == NULL){
        volume->last_error_code = -EINVAL;
        return NULL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 0, NULL, NULL);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return NULL;
    }
    // found a directory instead of a file
    if (entry.attributes & 0x10){ 
        volume->last_error_code = -EISDIR;
        return NULL;
    }
    // reading starts here
    uint8_t *buffer = malloc(entry.file_size);
    int remaining_bytes = entry.file_size;
    int offset = 0;
    uint16_t cluster = entry.first_logical_cluster;
    int sector = 97 + (cluster - 2);
    while (remaining_bytes > 0){
        offset = sector * SECTOR_SIZE;
        fseek(volume->image_buffer, offset, SEEK_SET);
        if (remaining_bytes >= SECTOR_SIZE){
            fread(buffer + (entry.file_size - remaining_bytes), SECTOR_SIZE, 1, volume->image_buffer);
            remaining_bytes -= SECTOR_SIZE;
        }
        else{
            fread(buffer + (entry.file_size - remaining_bytes), remaining_bytes, 1, volume->image_buffer);
            remaining_bytes = 0;
        }
        // find next logical cluster
        // go to start of the fat 
        // I can jump to the first cluster then just follow jumping around 
        fseek(volume->image_buffer, (SECTOR_SIZE * 1) + (cluster * sizeof(cluster)), SEEK_SET); // FAT1 STARTS AT SECTOR 1
        fread(&cluster, sizeof(cluster), 1, volume->image_buffer);
        sector = 97 + (cluster - 2);
    }
    *bytes_read = entry.file_size;
    return buffer;
}

/*
 * cat16_unlink -- unlink the file specified by path on the
 *      volume. This function should follow the approach we
 *      discussed in class. Mark the directory entry as
 *      deleted, and zero out its FAT entries. Leave the
 *      rest of its information (directory entry and
 *      contents) alone.
 * Return:
 *  - On success, it should return zero (0).
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG,
 *      ENOENT, and EISDIR.
 */
int cat16_unlink(cat16_volume_t *volume, const char *path){
    // check for invalid args 
    if (volume == NULL){
        return -EINVAL;
    }
    if (path == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 0, NULL, NULL);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    // found a directory instead of a file
    if (entry.attributes & 0x10){ 
        volume->last_error_code = -EISDIR;
        return -EISDIR;
    }
    // set every entry in the fat chain to 0x0000 and mark the directory entry as deleted
    entry.filename[0] = 0xE5; // mark the directory entry as deleted
    fseek(volume->image_buffer, entry.byte_offset, SEEK_SET);
    fwrite(entry.filename, sizeof(entry.filename), 1, volume->image_buffer);
    uint16_t cluster = entry.first_logical_cluster;
    while (cluster >= 0x0002 && cluster <= 0xFFEF){
        // find next logical cluster
        // go to start of the fat 
        // I can jump to the first cluster then just follow jumping around 
        //find next logical cluster
        uint16_t next_cluster = read_fat_entry(volume, cluster);
        // set the current cluster to 0x0000 in the fat
        write_fat_entry(volume, cluster, 0x0000);
        cluster = next_cluster;
    }
    return 0;
}

// to write a new file just sequentially search through clusters not in use
// if file already exists use any many allocated clusters as possible then add sqeuentiall
// alternatively you can unlink then the code is the same as 1. 

/*
 * cat16_writefile -- write a file with the specified name
 *      (path) to the mounted volume. If successful, the
 *      newly created file should have the length
 *      (bytes_to_write) and content (filedata) specified.
 *      If a file with that name already exists, it should be
 *      overwritten, and the old file data removed.
 * Return:
 *  - 0 (zero) on success.
 *  - On failures, it should return a negative error code.
 *      I should be able to pass this error code into the
 *      cat16_strerror() function, in order to get the
 *      corresponding error message. The standard system
 *      error message should be used for ENOSPC, EINVAL,
 *      ENAMETOOLONG, ENOENT, and ENOTDIR.
 */
int cat16_writefile(cat16_volume_t *volume, const char *path, uint8_t *filedata, size_t bytes_to_write){
    // check for invalid args 
    if (volume == NULL){
        return -EINVAL;
    }
    if (path == NULL || filedata == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    char filename[filename_bytes];
    char extension[extension_bytes];
    int result = path_handler(volume, path, &entry, 1, filename, extension);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    // found a directory instead of a file
    if (entry.entry_found == 1 && entry.attributes & 0x10){ 
        volume->last_error_code = -EISDIR;
        return -EISDIR;
    }
    // writing starts here two cases 
    // case 1: file already exists, unlink it and write new data
    if (entry.entry_found == 1){
        cat16_unlink(volume, path);
    }
    // after unlinking. case 1 is same as case 2 file does not exist
    // write the file to the image buffer and update the FAT table and dir entry
    strncpy(entry.filename, filename, sizeof(entry.filename));
    strncpy(entry.extension, extension, sizeof(entry.extension));
    int remaining_bytes = bytes_to_write;
    int offset = 0;
    uint16_t cluster = find_free_cluster(volume);
    if (cluster == 0xFFFF){
        volume->last_error_code = -ENOSPC;
        return -ENOSPC;
    }
    write_fat_entry(volume, cluster, 0xFFFF); // mark the first cluster as end of file in the FAT
    uint16_t first_logical_cluster = cluster;
    int sector = 97 + (cluster - 2);
    while (remaining_bytes > 0){
        // write data to the buffer
        offset = sector * SECTOR_SIZE;
        fseek(volume->image_buffer, offset, SEEK_SET);
        if (remaining_bytes >= SECTOR_SIZE){
            fwrite(filedata + (bytes_to_write - remaining_bytes), SECTOR_SIZE, 1, volume->image_buffer);
            remaining_bytes -= SECTOR_SIZE; 
        }
        else{
            fwrite(filedata + (bytes_to_write - remaining_bytes), remaining_bytes, 1, volume->image_buffer);
            remaining_bytes = 0;
        }
        //update the fat chain for the file.
        if (remaining_bytes > 0){
            uint16_t next_cluster = find_free_cluster(volume);
            if (next_cluster == 0xFFFF){
                volume->last_error_code = -ENOSPC;
                return -ENOSPC;
            }
            write_fat_entry(volume, cluster, next_cluster); // link the current cluster to the next cluster in the FAT
            write_fat_entry(volume, next_cluster, 0xFFFF); // mark the next cluster as end of file in the FAT
            cluster = next_cluster;
            sector = 97 + (cluster - 2);
        }
    }
    // add the directory entry for the new file
    result = add_dir_entry(volume, &entry, 0x20, first_logical_cluster, bytes_to_write); // 0x20 is the attribute for a normal file
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }

    return 0;
}


/*
 * cat16_listdir -- retrieve the contents (the entries) of
 *      the directory specified by the path argument on the
 *      specified mounted volume.
 * Return:
 *  - On success, it should return a pointer to a
 *      heap-allocated array of cat16_dirent_t structs, each
 *      representing a single directory entry. You should
 *      also store the number of entries returned in the
 *      memory pointed to by the "entries_read" argument.
 *      you should not include the "." and ".." entries in
 *      subdirectories.
 *
 *  - On failure, it should return NULL, and you should
 *      store a negative error code in the volume somewhere,
 *      so that I can get the error message by calling
 *      cat16_vstrerror(). The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG, ENOENT, and
 *      ENOTDIR.
 */
cat16_dirent_t *cat16_listdir(cat16_volume_t *volume, const char *path, size_t *entries_read){
    // check for invalid args
    if (volume == NULL){
        return NULL;
    }
    if (path == NULL || entries_read == NULL){
        volume->last_error_code = -EINVAL;
        return NULL;
    }
    //allocate memory for the directory entries
    cat16_dirent_t *dir_entries = malloc(sizeof(cat16_dirent_t) * MAX_DIR_ENTRIES);
    // search for the directory
    cat16_dir_entry_t entry;
    *entries_read = 0;
    if (strcmp(path, "/") == 0){
        entry.start_sector = ROOT_DIR_START_SECTOR;
        entry.end_sector = ROOT_DIR_END_SECTOR;
        // write a scan sector helper function to scan through a sector and fill in the dir_entries array
        for (int i = entry.start_sector; i < entry.end_sector; i++){
            scan_sector(volume, i, dir_entries, entries_read);
        }
        return dir_entries;
    }
    int result = path_handler(volume, path, &entry, 0, NULL, NULL);
    // check for errors when searching for the directory
    if (result < 0){
        volume->last_error_code = result;
        free(dir_entries);
        return NULL;
    }
    // found a file instead of a directory
    if (!(entry.attributes & 0x10)){
        volume->last_error_code = -ENOTDIR;
        free(dir_entries);
        return NULL;
    }
    
    // if root directory just look through sector 65-96

    // other wise I gotta search each sector and find the next one with the fat table
    uint16_t cluster = entry.first_logical_cluster;
    int sector = 97 + (cluster - 2);
    while (cluster >= 0x0002 && cluster <= 0xFFEF){
        scan_sector(volume, sector, dir_entries, entries_read);
        // find next logical cluster
        uint16_t next_cluster = read_fat_entry(volume, cluster);
        if (next_cluster == 0xFFFF){
            break;
        }
        cluster = next_cluster;
        sector = 97 + (cluster - 2);
    }
    return dir_entries;
}

/*
 * cat16_stat -- retrieve information about the file or
 *      directory pointed to by path on the specified volume.
 * Return:
 *  - On success, it should return zero (0). You should
 *      also store the requested information into the
 *      memory pointed to by the "out_stat" argument.
 *
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG, ENOENT, and
 *      ENOTDIR.
 */
int cat16_stat(cat16_volume_t *volume, const char *path, cat16_stat_t *out_stat){
    // check for invalid args 
    if (volume == NULL){
        return -EINVAL;
    }
    if (path == NULL || out_stat == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 0, NULL, NULL);
    // check for errors when searching for the file
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    out_stat->attributes = entry.attributes;
    out_stat->size = entry.file_size;
    out_stat->first_cluster = entry.first_logical_cluster;
    return 0;

}
/*
 * cat16_mkdir -- create a new directory with the specified
 *      name (path) on the volume. If the directory already
 *      exists don't overwrite it (EEXIST).
 * Return:
 *  - On success, it should return zero (0).
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EEXIST, EINVAL, ENAMETOOLONG,
 *      ENOENT, and ENOTDIR.
 */
int cat16_mkdir(cat16_volume_t *volume, const char *path){
    // check for invalid args 
    if (volume == NULL){
        return -EINVAL;
    }
    if (path == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    char filename[filename_bytes];
    char extension[extension_bytes];
    // see if the directory already exists
    cat16_dir_entry_t lookup;
    int result = path_handler(volume, path, &lookup, 1, filename, extension);
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    if (lookup.entry_found == 1){
        volume->last_error_code = -EEXIST;
        return -EEXIST;
    }
    // find a free cluster for the new directory
    uint16_t cluster = find_free_cluster(volume);
    if (cluster == 0xFFFF){
        volume->last_error_code = -ENOSPC;
        return -ENOSPC;
    }
    write_fat_entry(volume, cluster, 0xFFFF); // mark the first cluster as end of file in the FAT
    // zero out the new directory cluster (pass the CLUSTER, zero_out_cluster converts it to a sector)
    zero_out_cluster(volume, cluster);

    // write the directory entry for the new directory (into the parent. lookup.parent_cluster, 0 = root)
    cat16_dir_entry_t entry;
    strncpy(entry.filename, filename, sizeof(entry.filename));
    strncpy(entry.extension, extension, sizeof(entry.extension));
    entry.parent_cluster = lookup.parent_cluster;
    int dir_write_result = add_dir_entry(volume, &entry, 0x10, cluster, 0);
    if (dir_write_result < 0){
        write_fat_entry(volume, cluster, 0x0000); 
        volume->last_error_code = dir_write_result;
        return dir_write_result;
    }

    // write the "." entry (into the NEW directory)
    cat16_dir_entry_t dot_entry;
    memcpy(dot_entry.filename, ".       ", 8); //once again mem is used because c-strings are intended to have a null terminator
    memcpy(dot_entry.extension, "   ", 3);
    dot_entry.parent_cluster = cluster;
    write_dir_entry(volume, &dot_entry, 0x10, cluster, 0);
    // write the ".." entry (into the NEW directory, points at the parent. 0 if the parent is root)
    cat16_dir_entry_t dotdot_entry;
    memcpy(dotdot_entry.filename, "..      ", 8);
    memcpy(dotdot_entry.extension, "   ", 3);
    dotdot_entry.parent_cluster = cluster;
    write_dir_entry(volume, &dotdot_entry, 0x10, lookup.parent_cluster, 0);
    return 0;
}


/*
 * cat16_rmdir -- removes a directory with the specified
 *      name (path) on the volume. This function should
 *      follow the approach we discussed in class. Mark the
 *      directory entry as deleted, and zero out its FAT
 *      entries. Leave the rest of its information
 *      (directory entry and contents) alone.
 * Return:
 *  - On success, it should return zero (0).
 *  - On failure, it should return a negative error code.
 *      I should be able to pass this error code to
 *      cat16_strerror() to retrieve the corresponding error
 *      message. The standard system error messages
 *      should be used for EINVAL, ENAMETOOLONG,
 *      ENOENT, ENOTDIR, and ENOTEMPTY.
 */
int cat16_rmdir(cat16_volume_t *volume, const char *path){
    // check for invalid args 
    if (volume == NULL){
        return -EINVAL;
    }
    if(path == NULL){
        volume->last_error_code = -EINVAL;
        return -EINVAL;
    }
    // search for the directory 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry, 0, NULL, NULL);
    // check for errors when searching for the directory
    if (result < 0){
        volume->last_error_code = result;
        return result;
    }
    // found a file instead of a directory
    if (!(entry.attributes & 0x10)){ 
        volume->last_error_code = -ENOTDIR;
        return -ENOTDIR;
    }
    // check if the directory is empty (only contains "." and "..")
    size_t entries_read = 0;
    cat16_dirent_t *dir_entries = cat16_listdir(volume, path, &entries_read);
    if (dir_entries == NULL){
        volume->last_error_code = -ENOENT;
        return -ENOENT;
    }
    if (entries_read > 0){
        free(dir_entries);
        volume->last_error_code = -ENOTEMPTY;
        return -ENOTEMPTY;
    }
    free(dir_entries);
    // mark the directory entry as deleted and zero out its FAT entries
    entry.filename[0] = 0xE5; // mark the directory entry as
    entry.deleted = 1; // mark the directory entry as deleted
    fseek(volume->image_buffer, entry.byte_offset, SEEK_SET);
    fwrite(entry.filename, sizeof(entry.filename), 1, volume->image_buffer);
    uint16_t cluster = entry.first_logical_cluster;
    while (cluster  >= 0x0002 && cluster <= 0xFFEF){
        // find next logical cluster
        // go to start of the fat 
        // I can jump to the first cluster then just follow jumping around 
        int fat1_offset = (SECTOR_SIZE * 1) + (cluster * sizeof(cluster));
        int fat2_offset = (SECTOR_SIZE * 33) + (cluster * sizeof(cluster));

        //find next logical cluster
        fseek(volume->image_buffer, fat1_offset, SEEK_SET);
        fread(&cluster, sizeof(cluster), 1, volume->image_buffer);
        // zero out the fat entries in both FAT1 and FAT2
        uint16_t zero = 0x0000;
        fseek(volume->image_buffer, fat1_offset, SEEK_SET);
        fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
        fseek(volume->image_buffer, fat2_offset, SEEK_SET);
        fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
        // if the next logical cluster is 0xFFFF then I have reached the end of the file
        if (cluster == 0xFFFF){
            fseek(volume->image_buffer, fat1_offset, SEEK_SET);
            fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
            fseek(volume->image_buffer, fat2_offset, SEEK_SET);
            fwrite(&zero, sizeof(zero), 1, volume->image_buffer);
            break;
        }
    }
    return 0;
}
