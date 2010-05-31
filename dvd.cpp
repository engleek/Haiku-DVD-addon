/*
 * Copyright (c) 2005, David McPaul based on avi_reader copyright (c) 2004 Marcus Overhagen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "dvd.h"


#define TRACE_ASF_READER
#ifdef TRACE_ASF_READER
#   define TRACE printf
#else
#   define TRACE(a...)
#endif

#define ERROR(a...) fprintf(stderr, a)

dvdReader::dvdReader()
// :  theFileReader(0)
{
    //TRACE("dvdReader::dvdReader\n");
}


dvdReader::~dvdReader()
{}


const char *
dvdReader::Copyright()
{}


status_t
dvdReader::Sniff(int32 *streamCount)
{}


void
dvdReader::GetFileFormatInfo(media_file_format *mff)
{}

status_t
dvdReader::AllocateCookie(int32 streamNumber, void **_cookie)
{}


status_t
dvdReader::FreeCookie(void *_cookie)
{}


status_t
dvdReader::GetStreamInfo(void *_cookie, int64 *frameCount, bigtime_t *duration,
    media_format *format, const void **infoBuffer, size_t *infoSize)
{}


status_t
dvdReader::Seek(void *cookie, uint32 flags, int64 *frame, bigtime_t *time)
{}

status_t
dvdReader::FindKeyFrame(void* cookie, uint32 flags,
                            int64* frame, bigtime_t* time)
{}

status_t
dvdReader::GetNextChunk(void *_cookie, const void **chunkBuffer,
    size_t *chunkSize, media_header *mediaHeader)
{}


Reader *
dvdReaderPlugin::NewReader()
{
    return new dvdReader;
}


MediaPlugin *instantiate_plugin()
{
    return new dvdReaderPlugin;
}
