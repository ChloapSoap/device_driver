////////////////////////////////////////////////////////////////////////////////
//
//  File           : block_driver.c
//  Description    : This is the implementation of the standardized IO functions
//                   for used to access the BLOCK storage system.
//
//  Author         : Patrick McDaniel
//

// Includes
#include <stdlib.h>
#include <string.h>

// Project Includes
#include <block_controller.h>
#include <block_driver.h>
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>

#define OPEN 1
#define CLOSED 0

extern int freeFrameNr;

typedef char frame_t[BLOCK_FRAME_SIZE];

struct file_data {
    char name[128];
    int size;
    uint16_t frames[1024];
    int nrFrames;
};
typedef struct file_data file_t;

struct file_handler {
    file_t* file;
    int loc;
    int status;
};
typedef struct file_handler fh_t;

//cache prototypes
extern int init_block_cache(void);
extern int close_block_cache(void);
extern int put_block_cache(BlockIndex blk, BlockFrameIndex frm, void* frame);
extern void* get_block_cache(BlockIndex blk, BlockFrameIndex frm);

extern int compute_frame_checksum(void* frame, uint32_t* cs1);

//helper prototypes
BlockXferRegister pack(uint32_t ky1, uint32_t fm1, uint32_t cs1, uint32_t rt1);
void unpack(BlockXferRegister reg, uint32_t* ky1, uint32_t* fm1, uint32_t* cs1, uint32_t* rt1);
void closeAllFiles(fh_t* handles);
int createNewFile(const char* path, file_t* file);
int openFile(fh_t* handle, file_t* file);
void closeFile(fh_t* handle);
int verify_cs1(frame_t frame, uint32_t cs1);
void executeOpcode(frame_t frame, uint32_t ky1, uint32_t fm1);
int allocateNewFrames(fh_t* handle, int32_t count);
int getNbFiles(file_t* files);
int getFreeFrame(file_t* files);

// Global variables
int isOn = 0;
int nbFiles;
int nbHandles;
int freeFrameNr;
file_t files[BLOCK_MAX_TOTAL_FILES];
fh_t handles[BLOCK_MAX_TOTAL_FILES];

//
// Implementation

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_poweron
// Description  : Startup up the BLOCK interface, initialize filesystem
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t block_poweron(void)
{
    int i;
    // Check that the device is not already on
    if (isOn) {
        return -1;
    }
    // Call the INITMS opcode
    executeOpcode(NULL, BLOCK_OP_INITMS, 0);
    isOn = 1;
    // Call the BZERO opcode
    executeOpcode(NULL, BLOCK_OP_BZERO, 0);
    // Init the data structures
    for (i = 0; i < BLOCK_MAX_TOTAL_FILES; i++) {
        memset(&files[i], 0, sizeof(file_t));
        memset(&handles[i], 0, sizeof(fh_t));
    }
    nbHandles = 0;
    freeFrameNr = getFreeFrame(files);
    nbFiles = getNbFiles(files);
	//Initialize the cache
	printf("initializing the cache\n");
	if (init_block_cache()!=0)
		return -1;
    // Return successfully
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_poweroff
// Description  : Shut down the BLOCK interface, close all files
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t block_poweroff(void)
{
    // Check that the device is powered on
    if (!isOn) {
        return -1;
    }
    // Call the POWOFF opcode
    executeOpcode(NULL, BLOCK_OP_POWOFF, 0);
    // Close all files
    closeAllFiles(handles);
    // Free the data structures
    nbFiles = 0;
    nbHandles = 0;
    freeFrameNr = 0;
	//clear and cleanup the cache
	printf("closing the cache\n");
	if (close_block_cache()!=0)
		return -1;
    // Return successfully
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_open
// Description  : This function opens the file and returns a file handle
//
// Inputs       : path - filename of the file to open
// Outputs      : file handle if successful, -1 if failure

int16_t block_open(char* path)
{
    int i;
    int found;
    int16_t fd;
    // Check that the device is on
    if (!isOn) {
        return -1;
    }
    // Check if file exists
    found = 0;
    i = 0;
    while (i < nbFiles && !found) {
        if (memcmp(files[i].name, path, strlen(path)) == 0) {
            found = 1;
        } else {
            i++;
        }
    }
    // If no, create/init it
    if (!found) {
        createNewFile(path, &files[nbFiles]);
        i = nbFiles;
        nbFiles++;
    }
    // Open the file
    openFile(&handles[nbHandles], &files[i]);
    fd = nbHandles;
    nbHandles++;
    // THIS SHOULD RETURN A FILE HANDLE
    return (fd);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_close
// Description  : This function closes the file
//
// Inputs       : fd - the file descriptor
// Outputs      : 0 if successful, -1 if failure

int16_t block_close(int16_t fd)
{
    // Check that the device is on
    if (!isOn) {
        return -1;
    }
    // Check that fd is a valid file handler (file exists, is open, ...)
    if (handles[fd].status == CLOSED) {
        return -1;
    }
    // Set the file as closed
    closeFile(&handles[fd]);
    // Return successfully
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_read
// Description  : Reads "count" bytes from the file handle "fh" into the
//                buffer "buf"
//
// Inputs       : fd - filename of the file to read from
//                buf - pointer to buffer to read into
//                count - number of bytes to read
// Outputs      : bytes read if successful, -1 if failure

int32_t block_read(int16_t fd, void* buf, int32_t count)
{
    int32_t remaining;
    int32_t bufOffset;
    int32_t frame_offset;
    int32_t frame_nr;
    int32_t data_size;
    int32_t loc;
    int32_t fileSize;
    frame_t frame;
    file_t* file;
    // Check that the device is on
    if (!isOn) {
        return -1;
    }
    // Check that the file handle is correct (file exists, is open, ...)
    if (handles[fd].status == CLOSED) {
        return -1;
    }
    file = handles[fd].file;
    // Make sure we don't read more bytes than we have
    loc = handles[fd].loc;
    fileSize = file->size;
    if (fileSize - loc < count) {
    	count = fileSize - loc;
    }
    // While we haven't read `count` or reached the end of the file:
    remaining = count;
    bufOffset = 0;
    void *cacheBuf = NULL;
    while (remaining != 0) {
        frame_offset = loc % BLOCK_FRAME_SIZE;
        frame_nr = file->frames[loc / BLOCK_FRAME_SIZE];
		cacheBuf = NULL;
		cacheBuf = get_block_cache(0,frame_nr);
		if (cacheBuf != NULL) {
			memcpy(frame,cacheBuf,BLOCK_FRAME_SIZE); 
		}
		else {
        	//  Call the RDFRME opcode
        	executeOpcode(frame, BLOCK_OP_RDFRME, frame_nr);
			put_block_cache(0,frame_nr,frame);
		}
        //  Copy the relevant contents of the frame over to the buffer
        if (BLOCK_FRAME_SIZE - frame_offset > remaining) {
        	data_size = remaining;
        } else {
        	data_size = BLOCK_FRAME_SIZE - frame_offset;
        }
        memcpy(buf + bufOffset, frame + frame_offset, data_size);
        bufOffset += data_size;
        loc += data_size;
        remaining -= data_size;
	}
    handles[fd].loc = loc;
    // Return successfully
    return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_write
// Description  : Writes "count" bytes to the file handle "fh" from the
//                buffer  "buf"
//
// Inputs       : fd - filename of the file to write to
//                buf - pointer to buffer to write from
//                count - number of bytes to write
// Outputs      : bytes written if successful, -1 if failure

int32_t block_write(int16_t fd, void* buf, int32_t count)
{
    int32_t loc;
    int32_t remaining;
    int32_t frame_offset;
    int32_t frame_nr;
    int32_t bufOffset;
    int32_t data_size;
    file_t* file;
    frame_t frame;
    // Check that the file handle is correct (file exists, is open, ...)
    if (handles[fd].status == CLOSED) {
        return -1;
    }
    file = handles[fd].file;
    loc = handles[fd].loc;
    // If needed, add new frames to the file (to allow it to store all the new data)
    if (allocateNewFrames(&handles[fd], count) == -1) {
        return -1;
    }
    remaining = count;
    bufOffset = 0;
	void *cacheBuf = NULL;
    // While we have not written `count`:
    while (remaining > 0) {
        frame_nr = file->frames[loc / BLOCK_FRAME_SIZE];
        frame_offset = loc % BLOCK_FRAME_SIZE;
		//update the cache
		cacheBuf = NULL;
		cacheBuf = get_block_cache(0,frame_nr);
		if (cacheBuf != NULL) {
			memcpy(frame, cacheBuf, BLOCK_FRAME_SIZE); 
		}
		else {
        	executeOpcode(frame, BLOCK_OP_RDFRME, frame_nr);
			put_block_cache(0,frame_nr,frame);
		}
        //  Copy some of `buf` into the frame buffer
        if (BLOCK_FRAME_SIZE - frame_offset > remaining) {
            data_size = remaining;
        } else {
            data_size = BLOCK_FRAME_SIZE - frame_offset;
        }
        memcpy(frame + frame_offset, (char*)buf + bufOffset, data_size);
        //  Call the WRFRME opcode to write the frame buffer
        executeOpcode(frame, BLOCK_OP_WRFRME, frame_nr);
		put_block_cache(0,frame_nr,frame);
        loc += data_size;
        bufOffset += data_size;
        remaining -= data_size;
    }
    // Return successfully
    handles[fd].loc = loc;
    file->size += count;
    return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_read
// Description  : Seek to specific point in the file
//
// Inputs       : fd - filename of the file to write to
//                loc - offfset of file in relation to beginning of file
// Outputs      : 0 if successful, -1 if failure

int32_t block_seek(int16_t fd, uint32_t loc)
{
    // Check that the file handle is correct (file exists, is open, ...)
    if (handles[fd].status == CLOSED || handles[fd].file->size < loc) {
        return -1;
    }
    // Set the position to the desired location
    handles[fd].loc = loc;
    // Return successfully
    return (0);
}

// Packs the given register
BlockXferRegister pack(uint32_t ky1, uint32_t fm1, uint32_t cs1, uint32_t rt1)
{
    return ((uint64_t)ky1 << 56 | (uint64_t)fm1 << 40 | (uint64_t)cs1 << 8 | rt1);
}

// Unpacks the given register
void unpack(BlockXferRegister reg, uint32_t* ky1, uint32_t* fm1, uint32_t* cs1, uint32_t* rt1)
{
    *ky1 = reg >> 56;
    *fm1 = (reg << 8) >> 48;
    *cs1 = (reg << 24) >> 32;
    *rt1 = (reg << 56) >> 56;
    return;
}

// Closes all the file handles
void closeAllFiles(fh_t* handles)
{
    int i;
    for (i = 0; i < BLOCK_MAX_TOTAL_FILES; i++) {
        closeFile(&handles[i]);
    }
    return;
}

// Creates a new file with the given path
int createNewFile(const char* path, file_t* file)
{
    memcpy(file->name, path, strlen(path));
    file->size = 0;
    file->nrFrames = 0;
    return 0;
}

// Opens a new file handle to the given file
int openFile(fh_t* handle, file_t* file)
{
    handle->file = file;
    handle->loc = 0;
    handle->status = OPEN;
    return 0;
}

// Closes the given file handle
void closeFile(fh_t* handle)
{
    handle->status = CLOSED;
    handle->loc = -1;
    handle->file = NULL;
    return;
}

// Compares the checksum of the frame to the given checksum
int verify_cs1(frame_t frame, uint32_t cs1)
{
    uint32_t cs1_compute;
    compute_frame_checksum(frame, &cs1_compute);
    if (cs1 == cs1_compute) {
        return 0;
    } else {
        return -1;
    }
}

// Given a frame buffer, an instruction and a frame number,
// executes the instruction
void executeOpcode(frame_t frame, uint32_t ky1, uint32_t fm1)
{
    uint32_t rt1, cs1, cs1_comp;
    BlockXferRegister regstate;
    rt1 = -1;
    while (rt1 != 0) {
        if (ky1 == BLOCK_OP_WRFRME) {
            compute_frame_checksum(frame, &cs1);
        } else {
            cs1 = 0;
        }
        regstate = pack(ky1, fm1, cs1, 0);
        regstate = block_io_bus(regstate, frame);
        unpack(regstate, &ky1, &fm1, &cs1, &rt1);
        if (ky1 == BLOCK_OP_RDFRME) {
            compute_frame_checksum(frame, &cs1_comp);
            rt1 = (cs1 == cs1_comp) ? 0 : -1;
        }
    }
    return;
}

// Given a file handle and a number of bytes to write to a file,
// allocates as many frames as required to the file
int allocateNewFrames(fh_t* handle, int32_t count)
{
    uint16_t nrFrames;
    int32_t loc;
    nrFrames = handle->file->nrFrames;
    loc = handle->loc;
    while (loc + count > nrFrames * BLOCK_FRAME_SIZE) {
        handle->file->frames[nrFrames] = freeFrameNr;
        freeFrameNr++;
        nrFrames++;
        //  If we go over the max amount of frames, return -1
        if (freeFrameNr > BLOCK_BLOCK_SIZE) {
            return -1;
        }
    }
    handle->file->nrFrames = nrFrames;
    return 0;
}

// Given an array of files, returns the number of files
int getNbFiles(file_t* files)
{
    int i;
    for (i = 0; i < BLOCK_MAX_TOTAL_FILES; i++) {
        if (strlen(files[i].name) == 0) {
            return i;
        }
    }
    return i;
}

// Given an array of file_t, returns the number of the first frame unused by all the files
int getFreeFrame(file_t* files)
{
    int i;
    int j;
    int8_t frames[BLOCK_BLOCK_SIZE];
    memset(frames, 0, BLOCK_BLOCK_SIZE);
    // Set all the used frames to -1
    for (i = BLOCK_MAX_TOTAL_FILES; i < BLOCK_MAX_TOTAL_FILES; i++) {
        for (j = 0; j < files[i].nrFrames; j++) {
            frames[files[i].frames[j]] = -1;
        }
    }
    // Search for the first frame that is unused (i.e. has a 0 in the `frames` array)
    for (i = BLOCK_MAX_TOTAL_FILES; i < BLOCK_BLOCK_SIZE; i++) {
        if (frames[i] == 0) {
            return i;
        }
    }
    return -1;
}
