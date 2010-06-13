/*
    Copyright 1999, Be Incorporated.   All Rights Reserved.
    This file may be used under the terms of the Be Sample Code License.
*/
#include <Autolock.h>
#include <MediaFormats.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "DVDMediaAddOn.h"
#include "DVDMediaNode.h"

#define WIDTH 720
#define HEIGHT 576
#define FIELD_RATE 25.f

DVDMediaAddOn::DVDMediaAddOn(image_id imid)
    : BMediaAddOn(imid)
{
    // The formats largely depend on the DVD

    fMediaFormat.type = B_MEDIA_MULTISTREAM;
    fMediaFormat.u.multistream = media_multistream_format::wildcard;
    fMediaFormat.u.multistream.format = media_multistream_format::B_MPEG2;
    fMediaFormat.u.multistream.u.vid.frame_rate = FIELD_RATE;
    fMediaFormat.u.multistream.u.vid.width = WIDTH;
    fMediaFormat.u.multistream.u.vid.height = HEIGHT;

    fFlavorInfo.name = "DVD Media";
    fFlavorInfo.info = "DVD Media";
    fFlavorInfo.kinds = B_BUFFER_PRODUCER | B_CONTROLLABLE;
    fFlavorInfo.flavor_flags = 0;
    fFlavorInfo.internal_id = 0;
    fFlavorInfo.possible_count = 2; // Depends on number of drives really
    fFlavorInfo.in_format_count = 0;
    fFlavorInfo.in_format_flags = 0;
    fFlavorInfo.in_formats = NULL;
    fFlavorInfo.out_format_count = 1;
    fFlavorInfo.out_format_flags = 0;
    fFlavorInfo.out_formats = &fMediaFormat;

    fInitStatus = B_OK;
}

DVDMediaAddOn::~DVDMediaAddOn()
{
}


status_t
DVDMediaAddOn::InitCheck(const char **out_failure_text)
{
    if (fInitStatus < B_OK) {
        *out_failure_text = "Unknown error";
        return fInitStatus;
    }

    return B_OK;
}

int32
DVDMediaAddOn::CountFlavors()
{
    if (fInitStatus < B_OK)
        return fInitStatus;

    /* This addon only supports a single flavor, as defined in the
     * constructor */
    return 1;
}

/*
 * The pointer to the flavor received only needs to be valid between
 * successive calls to BMediaAddOn::GetFlavorAt().
 */
status_t
DVDMediaAddOn::GetFlavorAt(int32 n, const flavor_info **out_info)
{
    if (fInitStatus < B_OK)
        return fInitStatus;

    if (n != 0)
        return B_BAD_INDEX;

    /* Return the flavor defined in the constructor */
    *out_info = &fFlavorInfo;
    return B_OK;
}

BMediaNode *
DVDMediaAddOn::InstantiateNodeFor(
        const flavor_info *info, BMessage *config, status_t *out_error)
{
    DVDMediaNode *node;

    if (fInitStatus < B_OK)
        return NULL;

    if (info->internal_id != fFlavorInfo.internal_id)
        return NULL;

    /* At most one instance of the node should be instantiated at any given
     * time. The locking for this restriction may be found in the VideoProducer
     * class. */
    node = new DVDMediaNode(this, fFlavorInfo.name, fFlavorInfo.internal_id);
    if (node && (node->InitCheck() < B_OK)) {
        delete node;
        node = NULL;
    }

    return node;
}

BMediaAddOn *
make_media_addon(image_id imid)
{
    return new DVDMediaAddOn(imid);
}
