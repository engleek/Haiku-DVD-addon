/*
    Copyright 1999, Be Incorporated.   All Rights Reserved.
    This file may be used under the terms of the Be Sample Code License.
*/


#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include <Buffer.h>
#include <BufferGroup.h>
#include <ParameterWeb.h>
#include <TimeSource.h>

#include <Autolock.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

#include "dvdnav.h"

#define TOUCH(x) ((void)(x))

#define PRINTF(a,b) \
        do { \
            if (a < 2) { \
                printf("DVDMediaNode::"); \
                printf b; \
            } \
        } while (0)

#include "DVDMediaNode.h"

#define FIELD_RATE 30.f

#define DVD_LANGUAGE "en"

DVDMediaNode::DVDMediaNode(
        BMediaAddOn *addon, const char *name, int32 internal_id)
  : BMediaNode(name),
    BMediaEventLooper(),
    BBufferProducer(B_MEDIA_RAW_VIDEO),
    BControllable()
{
    fInitStatus = B_NO_INIT;

    fInternalID = internal_id;
    fAddOn = addon;

    fBufferGroup = NULL;

    fThread = -1;
    fFrameSync = -1;
    fProcessingLatency = 0LL;

    fRunning = false;
    fConnected = false;
    fEnabled = false;

    fOutput.destination = media_destination::null;

    _FindDrives("/dev/disk");
    SetDrive(0);

    fDVDLoaded = dvdnav_open(&dvdnav, GetDrivePath()) != DVDNAV_STATUS_OK;

    const char *path;
    dvdnav_path(dvdnav, &path);
    printf("Path: %s", path);

    dvdnav_menu_language_select(dvdnav, DVD_LANGUAGE);
    dvdnav_audio_language_select(dvdnav, DVD_LANGUAGE);
    dvdnav_spu_language_select(dvdnav, DVD_LANGUAGE);

    dvdnav_set_PGC_positioning_flag(dvdnav, 1);

    fInitStatus = B_OK;

    return;
}

DVDMediaNode::~DVDMediaNode()
{
    if (fInitStatus == B_OK) {
        /* Clean up after ourselves, in case the application didn't make us
         * do so. */
        if (fConnected)
            Disconnect(fOutput.source, fOutput.destination);
        if (fRunning)
            HandleStop();
    }
}

/* BMediaNode */

port_id
DVDMediaNode::ControlPort() const
{
    return BMediaNode::ControlPort();
}

BMediaAddOn *
DVDMediaNode::AddOn(int32 *internal_id) const
{
    if (internal_id)
        *internal_id = fInternalID;
    return fAddOn;
}

status_t
DVDMediaNode::HandleMessage(int32 message, const void *data, size_t size)
{
    return B_ERROR;
}

void
DVDMediaNode::Preroll()
{
    /* This hook may be called before the node is started to give the hardware
     * a chance to start. */
}

void
DVDMediaNode::SetTimeSource(BTimeSource *time_source)
{
    /* Tell frame generation thread to recalculate delay value */
    release_sem(fFrameSync);
}

status_t
DVDMediaNode::RequestCompleted(const media_request_info &info)
{
    return BMediaNode::RequestCompleted(info);
}

/* BMediaEventLooper */

void
DVDMediaNode::NodeRegistered()
{
    if (fInitStatus != B_OK) {
        ReportError(B_NODE_IN_DISTRESS);
        return;
    }

    /* Set up the parameter web */
    BParameterWeb *web = new BParameterWeb();
    BParameterGroup *main = web->MakeGroup(Name());
    BDiscreteParameter *state = main->MakeDiscreteParameter(
            P_COLOR, B_MEDIA_RAW_VIDEO, "Color", "Color");
    state->AddItem(B_HOST_TO_LENDIAN_INT32(0xff000000), "Block");
    state->AddItem(B_HOST_TO_LENDIAN_INT32(0x00ff0000), "Red");
    state->AddItem(B_HOST_TO_LENDIAN_INT32(0x0000ff00), "Green");
    state->AddItem(B_HOST_TO_LENDIAN_INT32(0x000000ff), "Blue");

    fColor = B_HOST_TO_LENDIAN_INT32(0x00ff0000);
    fLastColorChange = system_time();

    if (fDVDLoaded) {
        BDiscreteParameter *state = main->MakeDiscreteParameter(
                P_COLOR, B_MEDIA_RAW_VIDEO, "Load A-OK", B_ENABLE);
    } else {
        BDiscreteParameter *state = main->MakeDiscreteParameter(
                P_COLOR, B_MEDIA_RAW_VIDEO, "Load failed...", B_ENABLE);
    }

    /* After this call, the BControllable owns the BParameterWeb object and
     * will delete it for you */
    SetParameterWeb(web);

    fOutput.node = Node();
    fOutput.source.port = ControlPort();
    fOutput.source.id = 0;
    fOutput.destination = media_destination::null;
    strcpy(fOutput.name, Name());

    /* Tailor these for the output of your device */
    fOutput.format.type = B_MEDIA_RAW_VIDEO;
    fOutput.format.u.raw_video = media_raw_video_format::wildcard;
    fOutput.format.u.raw_video.interlace = 1;
    fOutput.format.u.raw_video.display.format = B_RGB32;

    /* Start the BMediaEventLooper control loop running */
    Run();
}

void
DVDMediaNode::Start(bigtime_t performance_time)
{
    BMediaEventLooper::Start(performance_time);
}

void
DVDMediaNode::Stop(bigtime_t performance_time, bool immediate)
{
    BMediaEventLooper::Stop(performance_time, immediate);
}

void
DVDMediaNode::Seek(bigtime_t media_time, bigtime_t performance_time)
{
    BMediaEventLooper::Seek(media_time, performance_time);
}

void
DVDMediaNode::TimeWarp(bigtime_t at_real_time, bigtime_t to_performance_time)
{
    BMediaEventLooper::TimeWarp(at_real_time, to_performance_time);
}

status_t
DVDMediaNode::AddTimer(bigtime_t at_performance_time, int32 cookie)
{
    return BMediaEventLooper::AddTimer(at_performance_time, cookie);
}

void
DVDMediaNode::SetRunMode(run_mode mode)
{
    BMediaEventLooper::SetRunMode(mode);
}

void
DVDMediaNode::HandleEvent(const media_timed_event *event,
        bigtime_t lateness, bool realTimeEvent)
{
    TOUCH(lateness); TOUCH(realTimeEvent);

    switch(event->type)
    {
        case BTimedEventQueue::B_START:
            HandleStart(event->event_time);
            break;
        case BTimedEventQueue::B_STOP:
            HandleStop();
            break;
        case BTimedEventQueue::B_WARP:
            HandleTimeWarp(event->bigdata);
            break;
        case BTimedEventQueue::B_SEEK:
            HandleSeek(event->bigdata);
            break;
        case BTimedEventQueue::B_HANDLE_BUFFER:
        case BTimedEventQueue::B_DATA_STATUS:
        case BTimedEventQueue::B_PARAMETER:
        default:
            PRINTF(-1, ("HandleEvent: Unhandled event -- %lx\n", event->type));
            break;
    }
}

void
DVDMediaNode::CleanUpEvent(const media_timed_event *event)
{
    BMediaEventLooper::CleanUpEvent(event);
}

bigtime_t
DVDMediaNode::OfflineTime()
{
    return BMediaEventLooper::OfflineTime();
}

void
DVDMediaNode::ControlLoop()
{
    BMediaEventLooper::ControlLoop();
}

status_t
DVDMediaNode::DeleteHook(BMediaNode * node)
{
    return BMediaEventLooper::DeleteHook(node);
}

/* BBufferProducer */

status_t
DVDMediaNode::FormatSuggestionRequested(
        media_type type, int32 quality, media_format *format)
{
    if (type != B_MEDIA_RAW_VIDEO)
        return B_MEDIA_BAD_FORMAT;

    TOUCH(quality);

    if (fOutput.format.u.raw_video.display.line_width == 0)
        fOutput.format.u.raw_video.display.line_width = 320;
    if (fOutput.format.u.raw_video.display.line_count == 0)
        fOutput.format.u.raw_video.display.line_count = 240;
    if (fOutput.format.u.raw_video.field_rate == 0)
        fOutput.format.u.raw_video.field_rate = 29.97f;

    *format = fOutput.format;
    return B_OK;
}

status_t
DVDMediaNode::FormatProposal(const media_source &output, media_format *format)
{
    status_t err;

    if (!format)
        return B_BAD_VALUE;

    if (output != fOutput.source)
        return B_MEDIA_BAD_SOURCE;

    err = format_is_compatible(*format, fOutput.format) ?
            B_OK : B_MEDIA_BAD_FORMAT;
    *format = fOutput.format;

    return err;
}

status_t
DVDMediaNode::FormatChangeRequested(const media_source &source,
        const media_destination &destination, media_format *io_format,
        int32 *_deprecated_)
{
    TOUCH(destination); TOUCH(io_format); TOUCH(_deprecated_);
    if (source != fOutput.source)
        return B_MEDIA_BAD_SOURCE;

    return B_ERROR;
}

status_t
DVDMediaNode::GetNextOutput(int32 *cookie, media_output *out_output)
{
    if (!out_output)
        return B_BAD_VALUE;

    if ((*cookie) != 0)
        return B_BAD_INDEX;

    *out_output = fOutput;
    (*cookie)++;
    return B_OK;
}

status_t
DVDMediaNode::DisposeOutputCookie(int32 cookie)
{
    TOUCH(cookie);

    return B_OK;
}

status_t
DVDMediaNode::SetBufferGroup(const media_source &for_source,
        BBufferGroup *group)
{
    TOUCH(for_source); TOUCH(group);

    return B_ERROR;
}

status_t
DVDMediaNode::VideoClippingChanged(const media_source &for_source,
        int16 num_shorts, int16 *clip_data,
        const media_video_display_info &display, int32 *_deprecated_)
{
    TOUCH(for_source); TOUCH(num_shorts); TOUCH(clip_data);
    TOUCH(display); TOUCH(_deprecated_);

    return B_ERROR;
}

status_t
DVDMediaNode::GetLatency(bigtime_t *out_latency)
{
    *out_latency = EventLatency() + SchedulingLatency();
    return B_OK;
}

status_t
DVDMediaNode::PrepareToConnect(const media_source &source,
        const media_destination &destination, media_format *format,
        media_source *out_source, char *out_name)
{
    PRINTF(1, ("PrepareToConnect() %ldx%ld\n", \
            format->u.raw_video.display.line_width, \
            format->u.raw_video.display.line_count));

    if (fConnected) {
        PRINTF(0, ("PrepareToConnect: Already connected\n"));
        return EALREADY;
    }

    if (source != fOutput.source)
        return B_MEDIA_BAD_SOURCE;

    if (fOutput.destination != media_destination::null)
        return B_MEDIA_ALREADY_CONNECTED;

    /* The format parameter comes in with the suggested format, and may be
     * specialized as desired by the node */
    if (!format_is_compatible(*format, fOutput.format)) {
        *format = fOutput.format;
        return B_MEDIA_BAD_FORMAT;
    }

    if (format->u.raw_video.display.line_width == 0)
        format->u.raw_video.display.line_width = 320;
    if (format->u.raw_video.display.line_count == 0)
        format->u.raw_video.display.line_count = 240;
    if (format->u.raw_video.field_rate == 0)
        format->u.raw_video.field_rate = 29.97f;

    *out_source = fOutput.source;
    strcpy(out_name, fOutput.name);

    fOutput.destination = destination;

    return B_OK;
}

void
DVDMediaNode::Connect(status_t error, const media_source &source,
        const media_destination &destination, const media_format &format,
        char *io_name)
{
    PRINTF(1, ("Connect() %ldx%ld\n", \
            format.u.raw_video.display.line_width, \
            format.u.raw_video.display.line_count));

    if (fConnected) {
        PRINTF(0, ("Connect: Already connected\n"));
        return;
    }

    if (    (source != fOutput.source) || (error < B_OK) ||
            !const_cast<media_format *>(&format)->Matches(&fOutput.format)) {
        PRINTF(1, ("Connect: Connect error\n"));
        return;
    }

    fOutput.destination = destination;
    strcpy(io_name, fOutput.name);

    if (fOutput.format.u.raw_video.field_rate != 0.0f) {
        fPerformanceTimeBase = fPerformanceTimeBase +
                (bigtime_t)
                    ((fFrame - fFrameBase) *
                    (1000000 / fOutput.format.u.raw_video.field_rate));
        fFrameBase = fFrame;
    }

    fConnectedFormat = format.u.raw_video;

    /* get the latency */
    bigtime_t latency = 0;
    media_node_id tsID = 0;
    FindLatencyFor(fOutput.destination, &latency, &tsID);
    #define NODE_LATENCY 1000
    SetEventLatency(latency + NODE_LATENCY);

    uint32 *buffer, *p, f = 3;
    p = buffer = (uint32 *)malloc(4 * fConnectedFormat.display.line_count *
            fConnectedFormat.display.line_width);
    if (!buffer) {
        PRINTF(0, ("Connect: Out of memory\n"));
        return;
    }
    bigtime_t now = system_time();
    for (int y = 0; y < (int)fConnectedFormat.display.line_count; y++)
        for (int x = 0; x < (int)fConnectedFormat.display.line_width; x++)
            *(p++) = ((((x+y)^0^x)+f) & 0xff) * (0x01010101 & fColor);
    fProcessingLatency = system_time() - now;
    free(buffer);

    /* Create the buffer group */
    fBufferGroup = new BBufferGroup(4 * fConnectedFormat.display.line_width *
            fConnectedFormat.display.line_count, 8);
    if (fBufferGroup->InitCheck() < B_OK) {
        delete fBufferGroup;
        fBufferGroup = NULL;
        return;
    }

    fConnected = true;
    fEnabled = true;

    /* Tell frame generation thread to recalculate delay value */
    release_sem(fFrameSync);
}

void
DVDMediaNode::Disconnect(const media_source &source,
        const media_destination &destination)
{
    PRINTF(1, ("Disconnect()\n"));

    if (!fConnected) {
        PRINTF(0, ("Disconnect: Not connected\n"));
        return;
    }

    if ((source != fOutput.source) || (destination != fOutput.destination)) {
        PRINTF(0, ("Disconnect: Bad source and/or destination\n"));
        return;
    }

    fEnabled = false;
    fOutput.destination = media_destination::null;

    fLock.Lock();
        delete fBufferGroup;
        fBufferGroup = NULL;
    fLock.Unlock();

    fConnected = false;
}

void
DVDMediaNode::LateNoticeReceived(const media_source &source,
        bigtime_t how_much, bigtime_t performance_time)
{
    TOUCH(source); TOUCH(how_much); TOUCH(performance_time);
}

void
DVDMediaNode::EnableOutput(const media_source &source, bool enabled,
        int32 *_deprecated_)
{
    TOUCH(_deprecated_);

    if (source != fOutput.source)
        return;

    fEnabled = enabled;
}

status_t
DVDMediaNode::SetPlayRate(int32 numer, int32 denom)
{
    TOUCH(numer); TOUCH(denom);

    return B_ERROR;
}

void
DVDMediaNode::AdditionalBufferRequested(const media_source &source,
        media_buffer_id prev_buffer, bigtime_t prev_time,
        const media_seek_tag *prev_tag)
{
    TOUCH(source); TOUCH(prev_buffer); TOUCH(prev_time); TOUCH(prev_tag);
}

void
DVDMediaNode::LatencyChanged(const media_source &source,
        const media_destination &destination, bigtime_t new_latency,
        uint32 flags)
{
    TOUCH(source); TOUCH(destination); TOUCH(new_latency); TOUCH(flags);
}

/* BControllable */

status_t
DVDMediaNode::GetParameterValue(
    int32 id, bigtime_t *last_change, void *value, size_t *size)
{
    if (id != P_COLOR)
        return B_BAD_VALUE;

    *last_change = fLastColorChange;
    *size = sizeof(uint32);
    *((uint32 *)value) = fColor;

    return B_OK;
}

void
DVDMediaNode::SetParameterValue(
    int32 id, bigtime_t when, const void *value, size_t size)
{
    if ((id != P_COLOR) || !value || (size != sizeof(uint32)))
        return;

    if (*(uint32 *)value == fColor)
        return;

    fColor = *(uint32 *)value;
    fLastColorChange = when;

    BroadcastNewParameterValue(
            fLastColorChange, P_COLOR, &fColor, sizeof(fColor));
}

status_t
DVDMediaNode::StartControlPanel(BMessenger *out_messenger)
{
    return BControllable::StartControlPanel(out_messenger);
}

/* DVDMediaNode */

void
DVDMediaNode::HandleStart(bigtime_t performance_time)
{
    /* Start producing frames, even if the output hasn't been connected yet. */

    PRINTF(1, ("HandleStart(%Ld)\n", performance_time));

    if (fRunning) {
        PRINTF(-1, ("HandleStart: Node already started\n"));
        return;
    }

    fFrame = 0;
    fFrameBase = 0;
    fPerformanceTimeBase = performance_time;

    fFrameSync = create_sem(0, "frame synchronization");
    if (fFrameSync < B_OK)
        goto err1;

    fThread = spawn_thread(_frame_generator_, "frame generator",
            B_NORMAL_PRIORITY, this);
    if (fThread < B_OK)
        goto err2;

    resume_thread(fThread);

    fRunning = true;
    return;

err2:
    delete_sem(fFrameSync);
err1:
    return;
}

void
DVDMediaNode::HandleStop(void)
{
    PRINTF(1, ("HandleStop()\n"));

    if (!fRunning) {
        PRINTF(-1, ("HandleStop: Node isn't running\n"));
        return;
    }

    delete_sem(fFrameSync);
    wait_for_thread(fThread, &fThread);

    fRunning = false;
}

void
DVDMediaNode::HandleTimeWarp(bigtime_t performance_time)
{
    fPerformanceTimeBase = performance_time;
    fFrameBase = fFrame;

    /* Tell frame generation thread to recalculate delay value */
    release_sem(fFrameSync);
}

void
DVDMediaNode::HandleSeek(bigtime_t performance_time)
{
    fPerformanceTimeBase = performance_time;
    fFrameBase = fFrame;

    /* Tell frame generation thread to recalculate delay value */
    release_sem(fFrameSync);
}

/* The following functions form the thread that generates frames. You should
 * replace this with the code that interfaces to your hardware. */
int32
DVDMediaNode::FrameGenerator()
{
    bigtime_t wait_until = system_time();

    while (1) {
        status_t err = acquire_sem_etc(fFrameSync, 1, B_ABSOLUTE_TIMEOUT,
                wait_until);

        /* The only acceptable responses are B_OK and B_TIMED_OUT. Everything
         * else means the thread should quit. Deleting the semaphore, as in
         * DVDMediaNode::HandleStop(), will trigger this behavior. */
        if ((err != B_OK) && (err != B_TIMED_OUT))
            break;

        fFrame++;

        /* Recalculate the time until the thread should wake up to begin
         * processing the next frame. Subtract fProcessingLatency so that
         * the frame is sent in time. */
        wait_until = TimeSource()->RealTimeFor(fPerformanceTimeBase +
                (bigtime_t)
                        ((fFrame - fFrameBase) *
                        (1000000 / fConnectedFormat.field_rate)), 0) -
                fProcessingLatency;

        /* Drop frame if it's at least a frame late */
        if (wait_until < system_time())
            continue;

        /* If the semaphore was acquired successfully, it means something
         * changed the timing information (see DVDMediaNode::Connect()) and
         * so the thread should go back to sleep until the newly-calculated
         * wait_until time. */
        if (err == B_OK)
            continue;

        /* Send buffers only if the node is running and the output has been
         * enabled */
        if (!fRunning || !fEnabled)
            continue;

        BAutolock _(fLock);

        /* Fetch a buffer from the buffer group */
        BBuffer *buffer = fBufferGroup->RequestBuffer(
                        4 * fConnectedFormat.display.line_width *
                        fConnectedFormat.display.line_count, 0LL);
        if (!buffer)
            continue;

        /* Fill out the details about this buffer. */
        media_header *h = buffer->Header();
        h->type = B_MEDIA_RAW_VIDEO;
        h->time_source = TimeSource()->ID();
        h->size_used = 4 * fConnectedFormat.display.line_width *
                        fConnectedFormat.display.line_count;
        /* For a buffer originating from a device, you might want to calculate
         * this based on the PerformanceTimeFor the time your buffer arrived at
         * the hardware (plus any applicable adjustments). */
        h->start_time = fPerformanceTimeBase +
                        (bigtime_t)
                            ((fFrame - fFrameBase) *
                            (1000000 / fConnectedFormat.field_rate));
        h->file_pos = 0;
        h->orig_size = 0;
        h->data_offset = 0;
        h->u.raw_video.field_gamma = 1.0;
        h->u.raw_video.field_sequence = fFrame;
        h->u.raw_video.field_number = 0;
        h->u.raw_video.pulldown_number = 0;
        h->u.raw_video.first_active_line = 1;
        h->u.raw_video.line_count = fConnectedFormat.display.line_count;

//        if (fColor == 0xff000000) {
//            // display a gray block that moves
//            uint32 *p = (uint32 *)buffer->Data();
//            for (int y = 0; y < (int)fConnectedFormat.display.line_count; y++)
//                for (int x = 0; x < (int)fConnectedFormat.display.line_width; x++) {
//                    if (x > (fFrame & 0xff) && x < (fFrame & 0xff) + 60 && y > 90 && y < 150) {
//                        *(p++) = 0xff777777;
//                    } else {
//                        *(p++) = 0x00000000;
//                    }
//                }
//        } else {
//
//            /* Fill in a pattern */
//            uint32 *p = (uint32 *)buffer->Data();
//            for (int y = 0; y < (int)fConnectedFormat.display.line_count; y++)
//                for (int x = 0; x < (int)fConnectedFormat.display.line_width; x++)
//                    *(p++) = ((((x+y)^0^x)+fFrame) & 0xff) * (0x01010101 & fColor);
//        }

 /*       int event, len;
        uint8_t *buf = mem;

        uint32 *p = (uint32 *)buffer->Data();
        dvdnav_get_next_block(dvdnav, buf, event, len);
        *p = (uint8_t *) buf;
*/
        /* Send the buffer on down to the consumer */
        if (SendBuffer(buffer, fOutput.source, fOutput.destination) < B_OK) {
            PRINTF(-1, ("FrameGenerator: Error sending buffer\n"));
            /* If there is a problem sending the buffer, return it to its
             * buffer group. */
            buffer->Recycle();
        }
    }

    return B_OK;
}

int32
DVDMediaNode::_frame_generator_(void *data)
{
    return ((DVDMediaNode *)data)->FrameGenerator();
}

bool
DVDMediaNode::SetDrive(const int32 &drive)
{
    BString *path = (BString*) fDriveList.ItemAt(drive);

    if (!path)
        return false;

    int device = open(path->String(), O_RDONLY);
    if (device >= 0) {
        //fFileHandle = device;
        fDrivePath = path;
        fDriveIndex = drive;
        return true;
    }

    return false;
}


const char *
DVDMediaNode::GetDrivePath() const
{
    if (!fDrivePath)
        return NULL;

    return fDrivePath->String();
}


int32
DVDMediaNode::_FindDrives(const char *path)
{
    BDirectory dir(path);

    if (dir.InitCheck() != B_OK)
        return B_ERROR;

    dir.Rewind();

    BEntry entry;
    while (dir.GetNextEntry(&entry) >= 0) {
        BPath path;
        const char *name;
        entry_ref e;

        if (entry.GetPath(&path) != B_OK)
            continue;

        name = path.Path();
        if (entry.GetRef(&e) != B_OK)
            continue;

        if (entry.IsDirectory()) {
            // ignore floppy -- it is not silent
            if (strcmp(e.name, "floppy") == 0)
                continue;
            else if (strcmp(e.name, "ata") == 0)
                continue;

            // Note that if we check for the count here, we could
            // just search for one drive. However, we want to find *all* drives
            // that are available, so we keep searching even if we've found one
            _FindDrives(name);

        } else {
            int devfd;
            device_geometry g;

            // ignore partitions
            if (strcmp(e.name, "raw") != 0)
                continue;

            devfd = open(name, O_RDONLY);
            if (devfd < 0)
                continue;

            if (ioctl(devfd, B_GET_GEOMETRY, &g, sizeof(g)) >= 0) {
                if (g.device_type == B_CD)
                    fDriveList.AddItem(new BString(name));
            }
            close(devfd);
        }
    }
    return fDriveList.CountItems();
}
