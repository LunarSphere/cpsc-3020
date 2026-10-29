#include "cat16.h"
#include <stdio.h>
#include <errno.h>


struct cat16_volume{
    long image_size;
    uint16_t sector_size; // 2 byte int
    uint8_t sectors_per_cluster; // 1 byte int
    uint16_t sectors_per_fat;
    uint8_t number_fats;
    uint16_t total_sectors;
    unsigned char *image_buffer;
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
    cat16_volume_t *volume = (cat16_volume_t *)malloc(sizeof(cat16_volume_t));
    FILE *file = fopen(image_path, "rb");
    // image could not be mounted
    if (file == NULL) {
        free(volume);
        return NULL;
    }
    // determine the size of the image 
    fseek(file, 0, SEEK_END);
    volume->image_size = ftell(file); // current possiton of pointer in the stream. since we are at the end of the file, this will give us the size of the filev
    rewind(file); // move back to start of file
    // allocate memory for the exact size of the image
    unsigned char * image_buffer = (unsigned char *)malloc(volume->image_size);
    // image could not be mouunted
    if (image_buffer == NULL) {
        fclose(file);
        return NULL;
    }
    // read the disk image into the buffer
    size_t bytes_read = fread(image_buffer, 1, volume->image_size, file);
    if (bytes_read != volume->image_size) {
        free(image_buffer);
        fclose(file);
        return NULL;
    }
    // store the image buffer in the volume struct
    volume->image_buffer = image_buffer;
    // fseek moves file pointer to specified position in the file. 
    // SEEK_SET goes to the absolute position in the file. 
    // fread (where to store data, size of the data, number of times to read data, where its stored)
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
    fclose(file);
    // check that the image meets the expected format
    if (volume->image_size != 0x400000 || volume->sector_size != 512 || volume->sectors_per_cluster != 1 || volume->sectors_per_fat != 32 || volume->number_fats != 2 || volume->total_sectors != 0x2000) {
        free(image_buffer);
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
    if (volume == NULL) {
        return -EINVAL;
    }
    free(volume->image_buffer);
    free(volume);
    return 0;
}

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

// read a file from the volume at the specified path.
// returns
//  - on success: a pointer to the contents of the file (should be allocated on the heap).
//  - on error: NULL
// And, the number of bytes read should be stored at the address pointed to by "bytes_read".

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
    return NULL; 
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
}

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
    return NULL;
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
    return NULL;
}