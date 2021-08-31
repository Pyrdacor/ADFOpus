/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 */
/*! \file	adf_file.c
 *  \brief	File code.
 */

#include<stdlib.h>
#include<string.h>

#include"adf_util.h"
#include"adf_file.h"
#include"adf_str.h"
#include"defendian.h"
#include"adf_raw.h"
#include"adf_disk.h"
#include"adf_dir.h"
#include"adf_bitm.h"
#include"adf_cache.h"

extern struct Env adfEnv;

void adfFileTruncate(struct Volume *vol, SECTNUM nParent, char *name)
{

}


/*
 * adfFileFlush
 */
/*!	\brief	Flush a file.
 *	\param	file - the file to flush.
 *	\return	Void.
 *
 *	Flushes a file's datablocks from memory to disk.
 */
void adfFlushFile(struct File *file)
{
    struct bEntryBlock parent;
    struct bOFSDataBlock *data;

    if (file->currentExt) {
        if (file->writeMode)
            adfWriteFileExtBlock(file->volume, file->currentExt->headerKey,
                file->currentExt);
    }
    if (file->currentData) {
        if (file->writeMode) {
            file->fileHdr->byteSize = file->pos;
	        if (isOFS(file->volume->dosType)) {
                data = (struct bOFSDataBlock *)file->currentData;
                data->dataSize = file->posInDataBlk;
            }
            if (file->fileHdr->byteSize>0)
                adfWriteDataBlock(file->volume, file->curDataPtr, 
				    file->currentData);
        }
    }
    if (file->writeMode) {
        file->fileHdr->byteSize = file->pos;

#ifdef _DEBUG_PRINTF_
		printf("pos=%ld\n",file->pos);
#endif /*_DEBUG_PRINTF_*/

        adfTime2AmigaTime(adfGiveCurrentTime(),
            &(file->fileHdr->days),&(file->fileHdr->mins),&(file->fileHdr->ticks) );
        adfWriteFileHdrBlock(file->volume, file->fileHdr->headerKey, file->fileHdr);

	    if (isDIRCACHE(file->volume->dosType)) {

#ifdef _DEBUG_PRINTF_
			printf("parent=%ld\n",file->fileHdr->parent);
#endif /*_DEBUG_PRINTF_*/

            adfReadEntryBlock(file->volume, file->fileHdr->parent, &parent);
            adfUpdateCache(file->volume, &parent, (struct bEntryBlock*)file->fileHdr,FALSE);
        }
        adfUpdateBitmap(file->volume);
    }
}


/*
 * adfGetFileBlocks
 *
 */
RETCODE adfGetFileBlocks(struct Volume* vol, struct bFileHeaderBlock* entry,
    struct FileBlocks* fileBlocks)
{
    long n, m;
    SECTNUM nSect;
    struct bFileExtBlock extBlock;
    long i;

    fileBlocks->header = entry->headerKey;
    adfFileRealSize( entry->byteSize, vol->datablockSize, 
        &(fileBlocks->nbData), &(fileBlocks->nbExtens) );

    fileBlocks->data=(SECTNUM*)malloc(fileBlocks->nbData * sizeof(SECTNUM));
    if (!fileBlocks->data) {
        (*adfEnv.eFct)("adfGetFileBlocks : malloc");
        return RC_MALLOC;
    }

    fileBlocks->extens=(SECTNUM*)malloc(fileBlocks->nbExtens * sizeof(SECTNUM));
    if (!fileBlocks->extens) {
        (*adfEnv.eFct)("adfGetFileBlocks : malloc");
        return RC_MALLOC;
    }
 
    n = m = 0;	
    /* in file header block */
    for(i=0; i<entry->highSeq; i++)
        fileBlocks->data[n++] = entry->dataBlocks[MAX_DATABLK-1-i];

    /* in file extension blocks */
    nSect = entry->extension;
    while(nSect!=0) {
        fileBlocks->extens[m++] = nSect;
        adfReadFileExtBlock(vol, nSect, &extBlock);
        for(i=0; i<extBlock.highSeq; i++)
            fileBlocks->data[n++] = extBlock.dataBlocks[MAX_DATABLK-1-i];
        nSect = extBlock.extension;
    }
    if ( (fileBlocks->nbExtens+fileBlocks->nbData) != (n+m) )
        (*adfEnv.wFct)("adfGetFileBlocks : less blocks than expected");

    return RC_OK;
}

/*
 * adfFreeFileBlocks
 *
 */
RETCODE adfFreeFileBlocks(struct Volume* vol, struct bFileHeaderBlock *entry)
{
    int i;
    struct FileBlocks fileBlocks;
    RETCODE rc = RC_OK;

    adfGetFileBlocks(vol,entry,&fileBlocks);

    for(i=0; i<fileBlocks.nbData; i++) {
        adfSetBlockFree(vol, fileBlocks.data[i]);
    }
    for(i=0; i<fileBlocks.nbExtens; i++) {
        adfSetBlockFree(vol, fileBlocks.extens[i]);
    }

    free(fileBlocks.data);
    free(fileBlocks.extens);
		
    return rc;
}


/*
 * adfFileRealSize
 *
 * Compute and return real number of block used by one file
 * Compute number of datablocks and file extension blocks
 *
 */
/*!	\brief	Compute and return real number of blocks used by one file, number of datablocks and file extension blocks.
 *	\param	size      - the file's size.
 *	\param	blockSize - The volume's data block size.
 *	\param	dataN     - If not NULL, a pointer to a long to receive the number of data blocks used.
 *	\param	extN      - If not NULL, a pointer to a long to receive the number of file extension used.
 *	\return	Returns the real size in blocks of a file with the given size.
 *
 *	The blockSize must be 488 or 512. This information is located in the datablockSize of the Volume structure.
 *	If the pointers dataN and extN aren't NULL, the number of data blocks and file extension blocks are returned.
 *	The return value does not take into account the new dircache that -may- be allocated.
 */
long adfFileRealSize(unsigned long size, int blockSize, long *dataN, long *extN)
{
    long data, ext;

   /*--- number of data blocks ---*/
    data = size / blockSize;
    if ( size % blockSize )
        data++;

    /*--- number of header extension blocks ---*/
    ext = 0;
    if (data>MAX_DATABLK) {
        ext = (data-MAX_DATABLK) / MAX_DATABLK;
        if ( (data-MAX_DATABLK) % MAX_DATABLK )
            ext++;
    }

    if (dataN)
        *dataN = data;
    if (extN)
        *extN = ext;
		
    return(ext+data+1);
}


/*
 * adfWriteFileHdrBlock
 *
 */
RETCODE adfWriteFileHdrBlock(struct Volume *vol, SECTNUM nSect, struct bFileHeaderBlock* fhdr)
{
    unsigned char buf[512];
    unsigned long newSum;
    RETCODE rc = RC_OK;

#ifdef _DEBUG_PRINTF_
	printf("adfWriteFileHdrBlock %ld\n",nSect);
#endif /*_DEBUG_PRINTF_*/

    fhdr->type = T_HEADER;
    fhdr->dataSize = 0;
    fhdr->secType = ST_FILE;

    memcpy(buf, fhdr, sizeof(struct bFileHeaderBlock));
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_FILE);
#endif
    newSum = adfNormalSum(buf,20,sizeof(struct bFileHeaderBlock));
    swLong(buf+20, newSum);
/*    *(unsigned long*)(buf+20) = swapLong((unsigned char*)&newSum);*/

    adfWriteBlock(vol, nSect, buf);

    return rc;
}


/*
 * adfFileSeek
 */
/*!	\brief	Seek within a file.
 *	\param	file - the file to seek within.
 *	\param	pos  - the position to seek to.
 *	\return	Void.
 */
void adfFileSeek(struct File *file, unsigned long pos)
{
    SECTNUM extBlock, nSect;
    unsigned long nPos;
    int i;
    
    nPos = min(pos, file->fileHdr->byteSize);
    file->pos = nPos;
    extBlock = adfPos2DataBlock(nPos, file->volume->datablockSize,
        &(file->posInExtBlk), &(file->posInDataBlk), &(file->curDataPtr) );
    if (extBlock==-1) {
        adfReadDataBlock(file->volume,
            file->fileHdr->dataBlocks[MAX_DATABLK-1-file->curDataPtr],
            file->currentData);
    }
    else {
        nSect = file->fileHdr->extension;
        i = 0;
        while( i<extBlock && nSect!=0 ) {
            adfReadFileExtBlock(file->volume, nSect, file->currentExt );
            nSect = file->currentExt->extension;
        }
        if (i!=extBlock)
            (*adfEnv.wFct)("error");
        adfReadDataBlock(file->volume,
            file->currentExt->dataBlocks[file->posInExtBlk], file->currentData);
    }
}


/*
 * adfOpenFile
 */ 
/*!	\brief	Open a file in an ADF.
 *	\param	vol  - a pointer to the current volume structure.
 *	\param	name - the file's name.
 *	\param	mode - access mode.
 *	\return	The File structure, ready to be read or written to. NULL if an error occurs : file not found with "r",
 *			or file already exists with "w".
 *
 *	Opens the file with the name "name" which is located in the current working directory of "vol".
 *	The allowable modes are "r" and "w". If the mode is "w", the file mustn't already exist, otherwise an error occurs.
 *	Some basic access permissions are just checked for now.
 *
 *	Available access modes are "r" = read, "w" = write, "a" = append.
 */
struct File* adfOpenFile(struct Volume *vol, char* name, char *mode)
{
    struct File *file;
    SECTNUM nSect;
    struct bEntryBlock entry, parent;
    BOOL write;
    char filename[200];

    write=( strcmp("w",mode)==0 || strcmp("a",mode)==0 );
    
	if (write && vol->dev->readOnly) {
        (*adfEnv.wFct)("adfFileOpen : device is mounted 'read only'");
        return NULL;
    }

    adfReadEntryBlock(vol, vol->curDirPtr, &parent);

    nSect = adfNameToEntryBlk(vol, parent.hashTable, name, &entry, NULL);
    if (!write && nSect==-1) {
        sprintf(filename,"adfFileOpen : file \"%s\" not found.",name);
        (*adfEnv.wFct)(filename);

#ifdef _DEBUG_PRINTF_
	fprintf(stdout,"filename %s %d, parent =%d\n",name,strlen(name),vol->curDirPtr);
#endif /*_DEBUG_PRINTF_*/

		 return NULL; 
    }
    if (!write && hasR(entry.access)) {
        (*adfEnv.wFct)("adfFileOpen : access denied"); return NULL; }
/*    if (entry.secType!=ST_FILE) {
        (*adfEnv.wFct)("adfFileOpen : not a file"); return NULL; }
	if (write && (hasE(entry.access)||hasW(entry.access))) {
        (*adfEnv.wFct)("adfFileOpen : access denied"); return NULL; }  
*/    if (write && nSect!=-1) {
        (*adfEnv.wFct)("adfFileOpen : file already exists"); return NULL; }  

    file = (struct File*)malloc(sizeof(struct File));
    if (!file) { (*adfEnv.wFct)("adfFileOpen : malloc"); return NULL; }
    file->fileHdr = (struct bFileHeaderBlock*)malloc(sizeof(struct bFileHeaderBlock));
    if (!file->fileHdr) {
		(*adfEnv.wFct)("adfFileOpen : malloc"); 
		free(file); return NULL; 
    }
    file->currentData = malloc(512*sizeof(unsigned char));
    if (!file->currentData) { 
		(*adfEnv.wFct)("adfFileOpen : malloc"); 
        free(file->fileHdr); free(file); return NULL; 
    }

    file->volume = vol;
    file->pos = 0;
    file->posInExtBlk = 0;
    file->posInDataBlk = 0;
    file->writeMode = write;
    file->currentExt = NULL;
    file->nDataBlock = 0;

    if (strcmp("w",mode)==0) {
        memset(file->fileHdr,0,512);
        adfCreateFile(vol,vol->curDirPtr,name,file->fileHdr);
        file->eof = TRUE;
    }
    else if (strcmp("a",mode)==0) {
        memcpy(file->fileHdr,&entry,sizeof(struct bFileHeaderBlock));
        file->eof = TRUE;
        adfFileSeek(file, file->fileHdr->byteSize);
    }
    else if (strcmp("r",mode)==0) {
        memcpy(file->fileHdr,&entry,sizeof(struct bFileHeaderBlock));
        file->eof = FALSE;
    }

/*puts("adfOpenFile");*/
    return(file);
}


/*
 * adfCloseFile
 */
/*!	\brief	Close a file.
 *	\param	file - a pointer to a file structure containing the file to close.
 *	\return	Void.
 *
 *	Calls adfFlushFile() and frees the file structure.
 */
void adfCloseFile(struct File *file)
{

    if (file==0)
        return;

#ifdef _DEBUG_PRINTF_
	puts("adfCloseFile in");
#endif /*_DEBUG_PRINTF_*/

    adfFlushFile(file);

    if (file->currentExt)
        free(file->currentExt);
    
    if (file->currentData)
        free(file->currentData);
    
    free(file->fileHdr);
    free(file);

#ifdef _DEBUG_PRINTF_
	puts("adfCloseFile out");
#endif /*_DEBUG_PRINTF_*/
}


/*
 * adfReadFile
 */
/*!	\brief	Read n bytes from the given file into a buffer.
 *	\param	file   - the file to read from.
 *	\param	n      - the number of bytes to read.
 *	\param	buffer - a buffer to receive the read bytes.
 *	\return	The number of bytes really read.
 *
 *	Use adfEndOfFile() to check if the end of the file is reached or not.
 *	See <A HREF="../../api_file.html">api_file.html</A> for example code.
 */
long adfReadFile(struct File* file, long n, unsigned char *buffer)
{
    long bytesRead;
    unsigned char *dataPtr, *bufPtr;
	int blockSize, size;

    if (n==0) return(n);
    blockSize = file->volume->datablockSize;

#ifdef _DEBUG_PRINTF_
	puts("adfReadFile");
#endif /*_DEBUG_PRINTF_*/

    if (file->pos+n > file->fileHdr->byteSize)
        n = file->fileHdr->byteSize - file->pos;

    if (isOFS(file->volume->dosType))
        dataPtr = (unsigned char*)(file->currentData)+24;
    else
        dataPtr = file->currentData;

    if (file->pos==0 || file->posInDataBlk==blockSize) {
        adfReadNextFileBlock(file);
        file->posInDataBlk = 0;
    }

    bytesRead = 0; bufPtr = buffer;
    size = 0;
    while ( bytesRead < n ) {
        size = min(n-bytesRead, blockSize-file->posInDataBlk);
        memcpy(bufPtr, dataPtr+file->posInDataBlk, size);
        bufPtr += size;
        file->pos += size;
        bytesRead += size;
        file->posInDataBlk += size;
        if (file->posInDataBlk==blockSize && bytesRead<n) {
            adfReadNextFileBlock(file);
            file->posInDataBlk = 0;
        }
    }
    file->eof = (file->pos==file->fileHdr->byteSize);
    return( bytesRead );
}


/*
 * adfEndOfFile
 */
/*! \brief	Check if the end of a file has been reached.
 *	\param	file - the file to check.
 *	\return	TRUE if the end of the file file has been reached. FALSE otherwise.
 */
BOOL adfEndOfFile(struct File* file)
{
    return(file->eof);
}


/*
 * adfReadNextFileBlock
 *
 */
RETCODE adfReadNextFileBlock(struct File* file)
{
    SECTNUM nSect;
    struct bOFSDataBlock *data;
    RETCODE rc = RC_OK;

    data =(struct bOFSDataBlock *) file->currentData;

    if (file->nDataBlock==0) {
        nSect = file->fileHdr->firstData;
    }
    else if (isOFS(file->volume->dosType)) {
        nSect = data->nextData;
    }
    else {
        if (file->nDataBlock<MAX_DATABLK)
            nSect = file->fileHdr->dataBlocks[MAX_DATABLK-1-file->nDataBlock];
        else {
            if (file->nDataBlock==MAX_DATABLK) {
                file->currentExt=(struct bFileExtBlock*)malloc(sizeof(struct bFileExtBlock));
                if (!file->currentExt) (*adfEnv.eFct)("adfReadNextFileBlock : malloc");
                adfReadFileExtBlock(file->volume, file->fileHdr->extension,
                    file->currentExt);
                file->posInExtBlk = 0;
            }
            else if (file->posInExtBlk==MAX_DATABLK) {
                adfReadFileExtBlock(file->volume, file->currentExt->extension,
                    file->currentExt);
                file->posInExtBlk = 0;
            }
            nSect = file->currentExt->dataBlocks[MAX_DATABLK-1-file->posInExtBlk];
            file->posInExtBlk++;
        }
    }
    adfReadDataBlock(file->volume,nSect,file->currentData);

    if (isOFS(file->volume->dosType) && data->seqNum!=file->nDataBlock+1)
        (*adfEnv.wFct)("adfReadNextFileBlock : seqnum incorrect");

    file->nDataBlock++;

    return rc;
}


/*
 * adfWriteFile
 */
/*!	\brief	Writes n bytes from the given buffer into a file.
 *	\param	file   - the file to write to.
 *	\param	n      - the number of bytes to write.
 *	\param	buffer - a buffe containing the data to write.
 *	\return	The number of bytes written.
 *
 *	See <A HREF="../../api_file.html">api_file.html</A> for example code.
 */
long adfWriteFile(struct File *file, long n, unsigned char *buffer)
{
    long bytesWritten;
    unsigned char *dataPtr, *bufPtr;
    int size, blockSize;
    struct bOFSDataBlock *dataB;

    if (n==0) return (n);

#ifdef _DEBUG_PRINTF_
	puts("adfWriteFile");
#endif /*_DEBUG_PRINTF_*/

    blockSize = file->volume->datablockSize;
    if (isOFS(file->volume->dosType)) {
        dataB =(struct bOFSDataBlock *)file->currentData;
        dataPtr = dataB->data;
    }
    else
        dataPtr = file->currentData;

    if (file->pos==0 || file->posInDataBlk==blockSize) {
        if (adfCreateNextFileBlock(file)==-1)
            (*adfEnv.wFct)("adfWritefile : no more free sector availbale");                        
        file->posInDataBlk = 0;
    }

    bytesWritten = 0; bufPtr = buffer;
    while( bytesWritten<n ) {
        size = min(n-bytesWritten, blockSize-file->posInDataBlk);
        memcpy(dataPtr+file->posInDataBlk, bufPtr, size);
        bufPtr += size;
        file->pos += size;
        bytesWritten += size;
        file->posInDataBlk += size;
        if (file->posInDataBlk==blockSize && bytesWritten<n) {
            if (adfCreateNextFileBlock(file)==-1)
                (*adfEnv.wFct)("adfWritefile : no more free sector availbale");                        
            file->posInDataBlk = 0;
        }
    }
    return( bytesWritten );
}


/*
 * adfCreateNextFileBlock
 *
 */
SECTNUM adfCreateNextFileBlock(struct File* file)
{
    SECTNUM nSect, extSect;
    struct bOFSDataBlock *data;
	unsigned int blockSize;
    int i;

#ifdef _DEBUG_PRINTF_
	puts("adfCreateNextFileBlock");
#endif /*_DEBUG_PRINTF_*/

    blockSize = file->volume->datablockSize;
    data = file->currentData;

    /* the first data blocks pointers are inside the file header block */
    if (file->nDataBlock<MAX_DATABLK) {
        nSect = adfGet1FreeBlock(file->volume);
        if (nSect==-1) return -1;

#ifdef _DEBUG_PRINTF_
	printf("adfCreateNextFileBlock fhdr %ld\n",nSect);
#endif /*_DEBUG_PRINTF_*/

        if (file->nDataBlock==0)
            file->fileHdr->firstData = nSect;
        file->fileHdr->dataBlocks[MAX_DATABLK-1-file->nDataBlock] = nSect;
        file->fileHdr->highSeq++;
    }
    else {
        /* one more sector is needed for one file extension block */
        if ((file->nDataBlock%MAX_DATABLK)==0) {
            extSect = adfGet1FreeBlock(file->volume);

#ifdef _DEBUG_PRINTF_
			printf("extSect=%ld\n",extSect);
#endif /*_DEBUG_PRINTF_*/

            if (extSect==-1) return -1;

            /* the future block is the first file extension block */
            if (file->nDataBlock==MAX_DATABLK) {
                file->currentExt=(struct bFileExtBlock*)malloc(sizeof(struct bFileExtBlock));
                if (!file->currentExt) {
                    adfSetBlockFree(file->volume, extSect);
                    (*adfEnv.eFct)("adfCreateNextFileBlock : malloc");
                    return -1;
                }
                file->fileHdr->extension = extSect;
            }

            /* not the first : save the current one, and link it with the future */
            if (file->nDataBlock>=2*MAX_DATABLK) {
                file->currentExt->extension = extSect;

#ifdef _DEBUG_PRINTF_
				printf ("write ext=%d\n",file->currentExt->headerKey);
#endif /*_DEBUG_PRINTF_*/

                adfWriteFileExtBlock(file->volume, file->currentExt->headerKey,
                    file->currentExt);
            }

            /* initializes a file extension block */
            for(i=0; i<MAX_DATABLK; i++)
                file->currentExt->dataBlocks[i] = 0L;
            file->currentExt->headerKey = extSect;
            file->currentExt->parent = file->fileHdr->headerKey;
            file->currentExt->highSeq = 0L;
            file->currentExt->extension = 0L;
            file->posInExtBlk = 0L;

#ifdef _DEBUG_PRINTF_
			printf("extSect=%ld\n",extSect);
#endif /*_DEBUG_PRINTF_*/

        }
        nSect = adfGet1FreeBlock(file->volume);
        if (nSect==-1) 
            return -1;
        
#ifdef _DEBUG_PRINTF_
		printf("adfCreateNextFileBlock ext %ld\n",nSect);
#endif /*_DEBUG_PRINTF_*/

        file->currentExt->dataBlocks[MAX_DATABLK-1-file->posInExtBlk] = nSect;
        file->currentExt->highSeq++;
        file->posInExtBlk++;
    }

    /* builds OFS header */
    if (isOFS(file->volume->dosType)) {
        /* writes previous data block and link it  */
        if (file->pos>=blockSize) {
            data->nextData = nSect;
            adfWriteDataBlock(file->volume, file->curDataPtr, file->currentData);

#ifdef _DEBUG_PRINTF_
			printf ("writedata=%d\n",file->curDataPtr);
#endif /*_DEBUG_PRINTF_*/

        }
        /* initialize a new data block */
        for(i=0; i<(int)blockSize; i++)
            data->data[i]=0;
        data->seqNum = file->nDataBlock+1;
        data->dataSize = blockSize;
        data->nextData = 0L;
        data->headerKey = file->fileHdr->headerKey;
    }
    else
        if (file->pos>=blockSize) {
            adfWriteDataBlock(file->volume, file->curDataPtr, file->currentData);

#ifdef _DEBUG_PRINTF_
			printf ("writedata=%d\n",file->curDataPtr);
#endif /*_DEBUG_PRINTF_*/

            memset(file->currentData,0,512);
        }
            
#ifdef _DEBUG_PRINTF_
	printf("datablk=%d\n",nSect);
#endif /*_DEBUG_PRINTF_*/

    file->curDataPtr = nSect;
    file->nDataBlock++;

    return(nSect);
}


/*
 * adfPos2DataBlock
 *
 */
long adfPos2DataBlock(long pos, int blockSize, 
    int *posInExtBlk, int *posInDataBlk, long *curDataN )
{
    long extBlock;

    *posInDataBlk = pos%blockSize;
    *curDataN = pos/blockSize;
    if (*posInDataBlk==0)
        (*curDataN)++;
    if (*curDataN<72) {
        *posInExtBlk = 0;
        return -1;
    }
    else {
        *posInExtBlk = (pos-72*blockSize)%blockSize;
        extBlock = (pos-72*blockSize)/blockSize;
        if (*posInExtBlk==0)
            extBlock++;
        return extBlock;
    }
}


/*
 * adfReadDataBlock
 *
 */
RETCODE adfReadDataBlock(struct Volume *vol, SECTNUM nSect, void *data)
{
    unsigned char buf[512];
    struct bOFSDataBlock *dBlock;
    RETCODE rc = RC_OK;

    adfReadBlock(vol, nSect,buf);

    memcpy(data,buf,512);

    if (isOFS(vol->dosType)) {
#ifdef LITT_ENDIAN
        swapEndian(data, SWBL_DATA);
#endif
        dBlock = (struct bOFSDataBlock*)data;

#ifdef _DEBUG_PRINTF_
	printf("adfReadDataBlock %ld\n",nSect);
#endif /*_DEBUG_PRINTF_*/

        if (dBlock->checkSum!=adfNormalSum(buf,20,sizeof(struct bOFSDataBlock)))
            (*adfEnv.wFct)("adfReadDataBlock : invalid checksum");
        if (dBlock->type!=T_DATA)
            (*adfEnv.wFct)("adfReadDataBlock : id T_DATA not found");
        if (dBlock->dataSize<0 || dBlock->dataSize>488)
            (*adfEnv.wFct)("adfReadDataBlock : dataSize incorrect");
        if ( !isSectNumValid(vol,dBlock->headerKey) )
			(*adfEnv.wFct)("adfReadDataBlock : headerKey out of range");
        if ( !isSectNumValid(vol,dBlock->nextData) )
			(*adfEnv.wFct)("adfReadDataBlock : nextData out of range");
    }

    return rc;
}


/*
 * adfWriteDataBlock
 *
 */
RETCODE adfWriteDataBlock(struct Volume *vol, SECTNUM nSect, void *data)
{
    unsigned char buf[512];
    unsigned long newSum;
    struct bOFSDataBlock *dataB;
    RETCODE rc = RC_OK;

    newSum = 0L;
    if (isOFS(vol->dosType)) {
        dataB = (struct bOFSDataBlock *)data;
        dataB->type = T_DATA;
        memcpy(buf,dataB,512);
#ifdef LITT_ENDIAN
        swapEndian(buf, SWBL_DATA);
#endif
        newSum = adfNormalSum(buf,20,512);
        swLong(buf+20,newSum);
/*        *(long*)(buf+20) = swapLong((unsigned char*)&newSum);*/
        adfWriteBlock(vol,nSect,buf);
    }
    else {
        adfWriteBlock(vol,nSect,data);
    }

#ifdef _DEBUG_PRINTF_
	printf("adfWriteDataBlock %ld\n",nSect);
#endif /*_DEBUG_PRINTF_*/

    return rc;
}


/*
 * adfReadFileExtBlock
 *
 */
RETCODE adfReadFileExtBlock(struct Volume *vol, SECTNUM nSect, struct bFileExtBlock* fext)
{
    unsigned char buf[sizeof(struct bFileExtBlock)];
    RETCODE rc = RC_OK;

    adfReadBlock(vol, nSect,buf);

#ifdef _DEBUG_PRINTF_
	printf("read fext=%d\n",nSect);
#endif /*_DEBUG_PRINTF_*/

    memcpy(fext,buf,sizeof(struct bFileExtBlock));
#ifdef LITT_ENDIAN
    swapEndian((unsigned char*)fext, SWBL_FEXT);
#endif
    if (fext->checkSum!=adfNormalSum(buf,20,sizeof(struct bFileExtBlock)))
        (*adfEnv.wFct)("adfReadFileExtBlock : invalid checksum");
    if (fext->type!=T_LIST)
        (*adfEnv.wFct)("adfReadFileExtBlock : type T_LIST not found");
    if (fext->secType!=ST_FILE)
        (*adfEnv.wFct)("adfReadFileExtBlock : stype  ST_FILE not found");
    if (fext->headerKey!=nSect)
        (*adfEnv.wFct)("adfReadFileExtBlock : headerKey!=nSect");
    if (fext->highSeq<0 || fext->highSeq>MAX_DATABLK)
        (*adfEnv.wFct)("adfReadFileExtBlock : highSeq out of range");
    if ( !isSectNumValid(vol, fext->parent) ) 
        (*adfEnv.wFct)("adfReadFileExtBlock : parent out of range");
    if ( fext->extension!=0 && !isSectNumValid(vol, fext->extension) )
        (*adfEnv.wFct)("adfReadFileExtBlock : extension out of range");

    return rc;
}


/*
 * adfWriteFileExtBlock
 *
 */
RETCODE adfWriteFileExtBlock(struct Volume *vol, SECTNUM nSect, struct bFileExtBlock* fext)
{
    unsigned char buf[512];
    unsigned long newSum;
    RETCODE rc = RC_OK;

    fext->type = T_LIST;
    fext->secType = ST_FILE;
    fext->dataSize = 0L;
    fext->firstData = 0L;

    memcpy(buf,fext,512);
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_FEXT);
#endif
    newSum = adfNormalSum(buf,20,512);
    swLong(buf+20,newSum);
/*    *(long*)(buf+20) = swapLong((unsigned char*)&newSum);*/

    adfWriteBlock(vol,nSect,buf);

    return rc;
}
/*###########################################################################*/
