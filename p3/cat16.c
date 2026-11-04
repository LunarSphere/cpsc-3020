#include "cat16.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#define filename_bytes 8
#define extension_bytes 3
#define FOUR_MB 0x400000
#define ROOT_DIR_START_SECTOR 65 // 65*512
#define ROOT_DIR_END_SECTOR 97 // 97*512
#define SECTOR_SIZE 512
#define SECTORS_PER_CLUSTER 1
#define SECTORS_PER_FAT 32
#define TOTAL_SECTORS 0x2000
#define path_depth 99
#define max_path_length 9999
#define ENTRY_SIZE 32


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

struct cat16_dir_entry{
    char filename[filename_bytes]; 
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
};



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
    FILE *file = fopen(image_path, "rb");
    // image could not be mounted | no such file or directory
    if (file == NULL){
        volume->last_error_code = ENOENT;
        free(volume);
        return NULL;
    }
    // determine the size of the image 
    fseek(file, 0, SEEK_END);
    volume->image_size = ftell(file); // current possiton of pointer in the stream. since we are at the end of the file, this will give us the size of the filev
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

// search directory for a file or directory with the specified filename.
// returns a struct representing the directory entry if found, else empty entry with entry_found = 0 
cat16_dir_entry_t search_directory(cat16_volume_t *volume, const char *filename, int start_sector, int end_sector){
    cat16_dir_entry_t entry;
    entry.entry_found = 0; // initialize entry_found to 0
    // search each availible sector for the dir entry
    for (int i = start_sector; i < end_sector; i++){
        // search each entry in the sector for the filename 512/32 16 entries per sector 
        for (int j  = 0; j < SECTOR_SIZE / ENTRY_SIZE; j++){
            // write the entry to the entry struct and see if it matches our filename
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
            entry.byte_offset = (i * SECTOR_SIZE) + (j * ENTRY_SIZE); // what byte the entry starts at

            // Disk stores name with padding ex: A.txt -> 'A       .txt'
            // so transform our name to match
            char name_on_disk[13];
            int len = 0; // need external tracker since file name and entry are seperate
            for (int k = 0; k < 8; k++){
                if (entry.filename[k] != ' '){
                    name_on_disk[len] = entry.filename[k];
                    len++;
                }
            }
            // if an extesion exists add a '.' and the extension to the name_on_disk
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
            // check if file name matches entries filename
            if (strcmp(name_on_disk, filename) == 0){
                entry.entry_found = 1;
                return entry;
            }
        }
    }
    return entry;
}

// walks through the provided path and searches for each file or directory in the path
// first we search through root directory then we just follow the first logical clusters. 
static int path_handler(cat16_volume_t *volume, const char *path, cat16_dir_entry_t *entry){
    int current_sector = ROOT_DIR_START_SECTOR; 
    int end_sector = ROOT_DIR_END_SECTOR; 
    entry->entry_found = 0; // initialize entry_found to 0

    const char *path_ptr = path; // so we can move through the path without modifying the original string
    // skip leading slashs
    while (*path_ptr == '/'){
        path_ptr++;
    }
    // search each entry
    while (*path_ptr != '\0'){
        size_t len = strcspn(path_ptr, "/"); // len of string up to / 
        char entry_name[13]; // 8 + . + 3 + null terminator
        // filename too long
        if (len > sizeof(entry_name)){
            return -ENAMETOOLONG; 
        }
        strncpy(entry_name, path_ptr, len);
        entry_name[len] = '\0'; 
        path_ptr += len; 
        //skip slashes 
        while(*path_ptr == '/'){
            path_ptr++;
        }
        *entry = search_directory(volume, entry_name, current_sector, end_sector);
        // no dir/file found
        if (entry->entry_found == 0){
            return -ENOENT;
        }
        // end of path reached
        if (*path_ptr == '\0'){
            return 0; 
        }
        // update the current sector to the first logical cluster of the found directory
        current_sector = 97 + (entry->first_logical_cluster - 2);
        end_sector = current_sector + 1;
    }
    return -ENOENT; // empty path
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
    if (volume == NULL || path == NULL || bytes_read == NULL){
        volume->last_error_code = -EINVAL;
        return NULL;
    }
    // search for the file 
    cat16_dir_entry_t entry;
    int result = path_handler(volume, path, &entry);
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
    int sector = 97 + (entry.first_logical_cluster - 2);
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
        sector++; // I dont understand how to get to the next logical cluster, incrementing the sector is wrong unless its continous.
    }
    *bytes_read = entry.file_size;   // after the loop, once
    return buffer;
}


// Need to get read file working before implementing write file.
// functions remaining 5


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
    return NULL;
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
    return 0;
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
    return 0;
    // cat16_dir_entry_t entry;
    // // check for invalid arguments
    // if (volume == NULL || path == NULL){
    //     return -EINVAL;
    // }
    // // start at the root directory
    // int current_sector = ROOT_DIR_START_SECTOR; // root directory starts at sector 65
    // int end_sector = 96; // end sector of root directory is 96

    // // path handling
    // char *path_array[path_depth];
    // split_path(path, path_array);

    // // search for each filname in the path array
    // for (int i = 0; i < path_depth; i++){
    //     // search the current directory(root) for the filename
    //     entry = search_directory(volume, path_array[i], current_sector, end_sector);
    //     // if entry_name is a directory, continue searching in that directory
    //     if (entry.attributes == 0x10){ 
    //         current_sector = 97 + (entry.first_logical_cluster - 2);
    //         end_sector = current_sector + 1; // after root we just have 1 sector per dir. 
    //     }
    //     else{
    //         // we found a file 
    //         return -ENOTDIR;
    //     }
    // }
    // // no such file or directory found
    // if (entry.entry_found == 0){
    //     return -ENOENT;
    // }
    // // mark the directory as deleted
    // entry.filename[0] = 0xE5;
    // // write the entry back to the image buffer
    // fseek(volume->image_buffer, entry.byte_offset, SEEK_SET);
    // fwrite(entry.filename, sizeof(entry.filename), 1, volume->image_buffer);

    // // zero out the FAT1 entries for the directory
    // // don't really know what is going on here but i think everything above is fine 
    // return 0;
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
    return strerror(error_code);
}

