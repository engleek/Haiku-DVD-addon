
#ifndef _DVD_READER_H
#define _DVD_READER_H

#include "ReaderPlugin.h"
//#include "dvd_types.h"
//#include "dvd_reader.h"
//#include "nav_types.h"
//#include "ifo_types.h" /* For vm_cmd_t */
//#include "dvdnav.h"
//#include "dvdnav_events.h"


class dvdReader : public Reader
{
public:
                dvdReader();
                ~dvdReader();

    virtual const char*     Copyright();

    virtual status_t        Sniff(int32 *streamCount);

    virtual void            GetFileFormatInfo(media_file_format *mff);

    virtual status_t        AllocateCookie(int32 streamNumber, void **cookie);
    virtual status_t        FreeCookie(void *cookie);

    virtual status_t        GetStreamInfo(void *cookie, int64 *frameCount, bigtime_t *duration,
                              media_format *format, const void **infoBuffer, size_t *infoSize);

    virtual status_t        Seek(void *cookie, uint32 flags,
                                int64 *frame, bigtime_t *time);

    virtual status_t        FindKeyFrame(void* cookie, uint32 flags,
                                int64* frame, bigtime_t* time);

    virtual status_t        GetNextChunk(void *cookie,
                                const void **chunkBuffer, size_t *chunkSize,
                                media_header *mediaHeader);
};


class dvdReaderPlugin : public ReaderPlugin
{
public:
    Reader *NewReader();
};

MediaPlugin *instantiate_plugin();

#endif
