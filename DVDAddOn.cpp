/*
    Copyright 1999, Be Incorporated.   All Rights Reserved.
    This file may be used under the terms of the Be Sample Code License.
*/
#include <Autolock.h>
#include <MediaFormats.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "DVDAddOn.h"
#include "DVDDiskNode.h"
#include "DVDDemuxerNode.h"

DVDAddOn::DVDAddOn(image_id imid)
    : BMediaAddOn(imid)
{
    // Multistream format, output for media node, input for extractor node

    fMultistreamMediaFormat.type = B_MEDIA_MULTISTREAM;
    fMultistreamMediaFormat.u.multistream = media_multistream_format::wildcard;
    fMultistreamMediaFormat.u.multistream.format = media_multistream_format::B_MPEG2;

    // Final output formats for extractor node
    
    // Video stream
    fFinalStreamFormats[0].type = B_MEDIA_ENCODED_VIDEO;
    fFinalStreamFormats[0].u.encoded_video = media_encoded_video_format::wildcard;

    // Audio stream
    fFinalStreamFormats[0].type = B_MEDIA_ENCODED_AUDIO;
    fFinalStreamFormats[0].u.encoded_audio = media_encoded_audio_format::wildcard;

    // Subs stream, don't know how to deal with this yet
    fFinalStreamFormats[0].type = B_MEDIA_UNKNOWN_TYPE;
    
    // Note: There might be a fourth data stream

    // The media node flavor

    fMediaNodeFlavorInfo.name = "DVD Disk Node";
    fMediaNodeFlavorInfo.info = "Extracts media and navigation data from the DVD disk.";
    fMediaNodeFlavorInfo.kinds = B_BUFFER_PRODUCER | B_CONTROLLABLE;
    fMediaNodeFlavorInfo.flavor_flags = 0;
    fMediaNodeFlavorInfo.internal_id = 0;
    fMediaNodeFlavorInfo.possible_count = 2; // Depends on number of drives really
    fMediaNodeFlavorInfo.in_format_count = 0;
    fMediaNodeFlavorInfo.in_format_flags = 0;
    fMediaNodeFlavorInfo.in_formats = NULL;
    fMediaNodeFlavorInfo.out_format_count = 1;
    fMediaNodeFlavorInfo.out_format_flags = 0;
    fMediaNodeFlavorInfo.out_formats = &fMultistreamMediaFormat;

    // The extractor node flavour

    fExtractorNodeFlavorInfo.name = "DVD Extractor";
    fExtractorNodeFlavorInfo.info = "Splits the DVD media into seperate streams.";
    fExtractorNodeFlavorInfo.kinds = B_BUFFER_CONSUMER | B_BUFFER_PRODUCER;
    fExtractorNodeFlavorInfo.flavor_flags = 0;
    fExtractorNodeFlavorInfo.internal_id = 1;
    fExtractorNodeFlavorInfo.possible_count = 2; // Depends on number of drives really
    fExtractorNodeFlavorInfo.in_format_count = 1;
    fExtractorNodeFlavorInfo.in_format_flags = 0;
    fExtractorNodeFlavorInfo.in_formats = &fMultistreamMediaFormat;
    fExtractorNodeFlavorInfo.out_format_count = 3;
    fExtractorNodeFlavorInfo.out_format_flags = 0;
    fExtractorNodeFlavorInfo.out_formats = fFinalStreamFormats;

    fInitStatus = B_OK;
}

DVDAddOn::~DVDAddOn()
{
}


status_t
DVDAddOn::InitCheck(const char **out_failure_text)
{
    if (fInitStatus < B_OK) {
        *out_failure_text = "Unknown error";
        return fInitStatus;
    }

    return B_OK;
}

int32
DVDAddOn::CountFlavors()
{
    if (fInitStatus < B_OK)
        return fInitStatus;

    return 2;
}

/*
 * The pointer to the flavor received only needs to be valid between
 * successive calls to BMediaAddOn::GetFlavorAt().
 */
status_t
DVDAddOn::GetFlavorAt(int32 n, const flavor_info **out_info)
{
    if (fInitStatus < B_OK)
        return fInitStatus;

    switch (n) {
    case 0:
        *out_info = &fMediaNodeFlavorInfo;
        break;
    case 1:
        *out_info = &fExtractorNodeFlavorInfo;
        break;
    default:
        return B_BAD_INDEX;
        break;
    }
    
    return B_OK;
}

BMediaNode *
DVDAddOn::InstantiateNodeFor(
        const flavor_info *info,
        BMessage *config,
        status_t *out_error)
{
    if (fInitStatus < B_OK)
        return NULL;

    if (info->internal_id == fMediaNodeFlavorInfo.internal_id) {
        DVDDiskNode *node = new DVDDiskNode(this, fMediaNodeFlavorInfo.name, fMediaNodeFlavorInfo.internal_id);
        if (node && (node->InitCheck() < B_OK)) {
            delete node;
            return NULL;
        } else {
            return node;
        }
    } else if (info->internal_id == fExtractorNodeFlavorInfo.internal_id) {
        DVDDemuxerNode *node = new DVDDemuxerNode(&fExtractorNodeFlavorInfo, this);
        if (node && (node->InitCheck() < B_OK)) {
            delete node;
            return NULL;
        } else {
            return node;
        }
    } else {
        return NULL;
    }
}

BMediaAddOn *
make_media_addon(image_id image)
{
    return new DVDAddOn(image);
}
