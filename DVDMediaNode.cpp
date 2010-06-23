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

#define DVD_LANGUAGE "en"

//#define DVD_VIDEO_LB_LEN 4096

DVDMediaNode::DVDMediaNode(
        BMediaAddOn *addon, const char *name, int32 internal_id)
  : BMediaNode(name),
    BMediaEventLooper(),
    BBufferProducer(B_MEDIA_MULTISTREAM),
    BControllable()
{
    fInitStatus = B_NO_INIT;

    fInternalID = internal_id;
    fAddOn = addon;

    fBufferGroup = NULL;

    fThread = -1;
    fFrameSync = -1;
    fProcessingLatency = 50;

    fRunning = false;
    fConnected = false;
    fEnabled = false;

    fOutput.destination = media_destination::null;

    _FindDrives("/dev/disk");
    SetDrive(0);

    fDVDLoaded = dvdnav_open(&dvdnav, GetDrivePath()) != DVDNAV_STATUS_OK;

    const char *path;
    dvdnav_path(dvdnav, &path);
    printf("DVD Path: %s\n", path);

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

    fOutput.node = Node();
    fOutput.source.port = ControlPort();
    fOutput.source.id = 0;
    fOutput.destination = media_destination::null;
    strcpy(fOutput.name, Name());

    /* Video format config */
    fOutput.format.type = B_MEDIA_MULTISTREAM;
    fOutput.format.u.multistream = media_multistream_format::wildcard;
    fOutput.format.u.multistream.format = media_multistream_format::B_MPEG2;

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
    if (type != B_MEDIA_MULTISTREAM)
        return B_MEDIA_BAD_FORMAT;

    TOUCH(quality);

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
    PRINTF(1, ("PrepareToConnect()\n")); // Fill in format details here.

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
    PRINTF(1, ("Connect()\n")); // Again, complete with format details.

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

    fPerformanceTimeBase = fPerformanceTimeBase + fProcessingLatency;
    fFrameBase = fFrame;

    fConnectedFormat = format.u.multistream;

    /* get the latency */
    bigtime_t latency = 50;
    media_node_id tsID = 0;
    FindLatencyFor(fOutput.destination, &latency, &tsID);
    SetEventLatency(latency);

    finished = 0;
    output_fd = 0;
    dump = 0;
    tt_dump = 0;

/*    uint8_t *buffer, *p;
    p = buffer = (uint8_t *)malloc(DVD_VIDEO_LB_LEN);
    if (!buffer) {
        PRINTF(0, ("Connect: Out of memory\n"));
        return;
    }
    bigtime_t now = system_time();
    dvdnav_get_next_block(dvdnav, p, &event, &len);
    fProcessingLatency = system_time() - now;
    free(buffer); */

    fProcessingLatency = 50;

    /* Create the buffer group */
    fBufferGroup = new BBufferGroup(DVD_VIDEO_LB_LEN, 32); // Why 8?
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

status_t
DVDMediaNode::HandleMessage(int32 message, const void *data, size_t size)
{
    return B_ERROR;
}


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
    finished = 0;

    fFrameSync = create_sem(0, "frame synchronization");
    if (fFrameSync < B_OK)
        goto err1;

    fThread = spawn_thread(_stream_generator_, "stream generator",
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
DVDMediaNode::StreamGenerator()
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
        BBuffer *buffer = fBufferGroup->RequestBuffer(DVD_VIDEO_LB_LEN, 50);
        if (!buffer)
            continue;

        /* Fill out the details about this buffer. */
        media_header *h = buffer->Header();
        h->type = B_MEDIA_MULTISTREAM;
        h->time_source = TimeSource()->ID();
        h->size_used = DVD_VIDEO_LB_LEN;
        h->start_time = fPerformanceTimeBase + fProcessingLatency;
        h->file_pos = 0;
        h->orig_size = 0;
        h->data_offset = 0;


        result = dvdnav_get_next_block(dvdnav, (uint8_t *)buffer->Data(), &event, &len);

        if (result == DVDNAV_STATUS_ERR) {
            printf("DVD: Error getting next block: %s\n", dvdnav_err_to_string(dvdnav));
            return B_ERROR;
        }

        switch (event) {
        case DVDNAV_BLOCK_OK:
            // Regular MPEG block: Send the buffer on down to the consumer
            if (SendBuffer(buffer, fOutput.source, fOutput.destination) < B_OK) {
                printf("DVD: StreamGenerator: Error sending buffer\n");
                buffer->Recycle();
            }

            break;
        case DVDNAV_NOP:
            // No idea why this exists...
            break;
        case DVDNAV_STILL_FRAME:
            // Still frame: Find still time
            {
                dvdnav_still_event_t *still_event = (dvdnav_still_event_t *)buffer->Data();
                if (still_event->length < 0xff)
                    printf("DVD: Still frame: %d seconds\n", still_event->length);
                else
                    printf("DVD: Still frame: indefinite\n");
                dvdnav_still_skip(dvdnav);
            }
            break;
        case DVDNAV_WAIT:
            /* We have reached a point in DVD playback, where timing is critical.
            * Player application with internal fifos can introduce state
            * inconsistencies, because libdvdnav is always the fifo's length
            * ahead in the stream compared to what the application sees.
            * Such applications should wait until their fifos are empty
            * when they receive this type of event. */
            printf("DVD: Skipping wait condition\n");
            dvdnav_wait_skip(dvdnav);
            break;
        case DVDNAV_SPU_CLUT_CHANGE:
            // New colours!
            break;
        case DVDNAV_SPU_STREAM_CHANGE:
            // New stream
            break;
        case DVDNAV_AUDIO_STREAM_CHANGE:
            // Switch audio channels
            break;
        case DVDNAV_HIGHLIGHT:
            // Button highlight
            {
                dvdnav_highlight_event_t *highlight_event = (dvdnav_highlight_event_t *)buffer->Data();
                printf("DVD: Selected button %d\n", highlight_event->buttonN);
            }
            break;
        case DVDNAV_VTS_CHANGE:
            /* Some status information like video aspect and video scale permissions do
            * not change inside a VTS. Therefore this event can be used to query such
            * information only when necessary and update the decoding/displaying
            * accordingly. */
            break;
        case DVDNAV_CELL_CHANGE:
            /* Some status information like the current Title and Part numbers do not
            * change inside a cell. Therefore this event can be used to query such
            * information only when necessary and update the decoding/displaying
            * accordingly. */
            {
                int32_t tt = 0, ptt = 0;
                uint32_t pos, len;
                char input = '\0';

                dvdnav_current_title_info(dvdnav, &tt, &ptt);
                dvdnav_get_position(dvdnav, &pos, &len);
                printf("DVD: Cell change: Title %d, Chapter %d\n", tt, ptt);
                printf("DVD: At position %.0f%% inside the feature\n", 100 * (double)pos / (double)len);
            }
            break;
        case DVDNAV_NAV_PACKET:
            /* A NAV packet provides PTS discontinuity information, angle linking information and
            * button definitions for DVD menus. Angles are handled completely inside libdvdnav.
            * For the menus to work, the NAV packet information has to be passed to the overlay
            * engine of the player so that it knows the dimensions of the button areas. */
            {
                pci_t *pci;

                /* Applications with fifos should not use these functions to retrieve NAV packets,
                * they should implement their own NAV handling, because the packet you get from these
                * functions will already be ahead in the stream which can cause state inconsistencies.
                * Applications with fifos should therefore pass the NAV packet through the fifo
                * and decoding pipeline just like any other data. */
                pci = dvdnav_get_current_nav_pci(dvdnav);
                dvdnav_get_current_nav_dsi(dvdnav);

                if(pci->hli.hl_gi.btn_ns > 0) {
                    int button;

                    printf("DVD: Found %i DVD menu buttons...\n", pci->hli.hl_gi.btn_ns);

                    for (button = 0; button < pci->hli.hl_gi.btn_ns; button++) {
                        btni_t *btni = &(pci->hli.btnit[button]);
                        printf("DVD: Button %i top-left @ (%i,%i), bottom-right @ (%i,%i)\n",
                        button + 1, btni->x_start, btni->y_start,
                        btni->x_end, btni->y_end);
                    }

                    button = 1; // First button, generally the start film button

                    printf("DVD: Selecting button %i...\n", button);
                    /* This is the point where applications with fifos have to hand in a NAV packet
                    * which has traveled through the fifos. See the notes above. */
                    dvdnav_button_select_and_activate(dvdnav, pci, button);
                }
            }
            break;
        case DVDNAV_HOP_CHANNEL:
            /* This event is issued whenever a non-seamless operation has been executed.
            * Applications with fifos should drop the fifos content to speed up responsiveness. */
            break;
        case DVDNAV_STOP:
            /* Playback should end here. */
            {
                finished = 1;
            }
            break;
        default:
            printf("DVD: Unknown event (%i)\n", event);
            HandleStop();
            break;
        }
    }

    return B_OK;
}

int32
DVDMediaNode::_stream_generator_(void *data)
{
    return ((DVDMediaNode *)data)->StreamGenerator();
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
