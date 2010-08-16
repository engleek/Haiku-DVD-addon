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
                printf("DVDDiskNode::"); \
                printf b; \
            } \
        } while (0)

#include "DVDDiskNode.h"

#define DVD_LANGUAGE "en"

DVDDiskNode::DVDDiskNode(
        BMediaAddOn *addon, const char *name, int32 internal_id)
  : BMediaNode(name),
    BMediaEventLooper(),
    BBufferProducer(B_MEDIA_UNKNOWN_TYPE),

    fInternalID(internal_id),
    fAddOn(addon),

    fThread(-1),
    fStreamSync(-1),
    fProcessingLatency(50),

    fRunning(false),
    fConnected(false),
 
    fInitStatus(B_OK),
    
    fPosition(0)
{
    fBuffer = (uint8_t *) malloc (DVD_VIDEO_LB_LEN);
        
    LoadDisk();

    if (!fDVDLoaded) {
        PRINTF(1, ("Ouch, unable to load disk.\n"));
        fInitStatus = B_ERROR;
        return;
    }
    
    InitOutputs();
 
    return;
}

void
DVDDiskNode::LoadDisk()
{
    PRINTF(1, ("LoadDisk()\n"));
    
    _FindDrives("/dev/disk");
    SetDrive(0);

    fDVDLoaded = dvdnav_open(&fDVDNav, GetDrivePath()) == DVDNAV_STATUS_OK;

    if (fDVDLoaded) {
        const char *path;
        dvdnav_path(fDVDNav, &path);
        printf("DVD Path: %s\n", path);

        dvdnav_menu_language_select(fDVDNav, DVD_LANGUAGE);
        dvdnav_audio_language_select(fDVDNav, DVD_LANGUAGE);
        dvdnav_spu_language_select(fDVDNav, DVD_LANGUAGE);

        dvdnav_set_PGC_positioning_flag(fDVDNav, 1);

        //dvdnav_part_play(fDVDNav, 1, 1);
        
        fPerformanceTimeBase = fPerformanceTimeBase + fProcessingLatency;

        // Setting up some other stuff here for now

        SetEventLatency(50);
        fProcessingLatency = 50; // Work out latencies later.
    }
}

void
DVDDiskNode::InitOutputs()
{
    PRINTF(1, ("InitOutputs()\n"));
/*
    // Need to realloc for the extra blocks
    fBufferSize = 5 * DVD_VIDEO_LB_LEN;
    fBuffer = (uint8_t *) realloc(fBuffer, fBufferSize);
     
    int count = 0;
    while (count < 5) {
        fResult = dvdnav_get_next_block(fDVDNav, fBuffer + (DVD_VIDEO_LB_LEN * count), &fEvent, &fLen);

        if (fResult == DVDNAV_STATUS_ERR) {
            printf("DVD: Error getting block: %s\n", dvdnav_err_to_string(fDVDNav));
            return;
        }

        if (fEvent == DVDNAV_BLOCK_OK) {
            count++;
        }
    }
*/    
    PRINTF(1, ("InitOutputs(): BMediaFile\n"));
    
    fMediaFile = new BMediaFile(this, (long int) 0);

    int trackCount = fMediaFile->CountTracks();

    for (int32 i = 0; i < trackCount; i++) {
        PRINTF(1, ("InitOutputs(): Creating Track %i of %i\n", i, trackCount));
        BMediaTrack* track = fMediaFile->TrackAt(i);

        // Grab the encoded format
        media_format* encFormat = new media_format();
        track->EncodedFormat(encFormat);
        
        // Create output based on the new format       
        media_output *output = new media_output();

        output->node = Node();
        output->source.id = i;
        output->source.port = ControlPort();
        output->destination = media_destination::null;

        int bufferSize = 0;

        if (encFormat->IsVideo()) {
            PRINTF(1, ("InitOutputs(): Video Track\n"));
            output->format.type = B_MEDIA_RAW_VIDEO;
            output->format.u.raw_video = media_raw_video_format::wildcard;
            output->format.u.raw_video.display.line_width = encFormat->Width();
            output->format.u.raw_video.display.line_count = encFormat->Height();
            output->format.u.raw_video.display.format = encFormat->ColorSpace();
            
            //bufferSize = encFormat->Width() * encFormat->Height();
            bufferSize = 8147200;

            track->DecodedFormat(&output->format);
            
            // Create the buffer group for the output.
            BBufferGroup *bufferGroup = new BBufferGroup(bufferSize, 10); // 
            if (bufferGroup->InitCheck() < B_OK) {
                delete bufferGroup;
                bufferGroup = NULL;
                return;
            }

            PRINTF(1, ("InitOutputs(): Add output %i to the list...\n", i));
            fOutputs.push_back(output);
            fBufferGroups.push_back(bufferGroup);
            fTracks.push_back(track);
            fConnected.push_back(false);
        } else if (encFormat->IsAudio()) {
            PRINTF(1, ("InitOutputs(): Audio Track\n"));
            output->format.type = B_MEDIA_RAW_AUDIO;
            output->format.u.raw_audio = media_raw_audio_format::wildcard;
            output->format.u.raw_audio.format = encFormat->AudioFormat();
            //output->format.u.raw_audio.frame_size = encFormat->AudioFrameSize();
            
            bufferSize = encFormat->u.raw_audio.buffer_size;

            track->DecodedFormat(&output->format);
            
            // Create the buffer group for the output.
            BBufferGroup *bufferGroup = new BBufferGroup(bufferSize, 10); // 
            if (bufferGroup->InitCheck() < B_OK) {
                delete bufferGroup;
                bufferGroup = NULL;
                return;
            }

            PRINTF(1, ("InitOutputs(): Add output %i to the list...\n", i));
            fOutputs.push_back(output);
            fBufferGroups.push_back(bufferGroup);
            fTracks.push_back(track);
            fConnected.push_back(false);
        } else {
            PRINTF(1, ("InitOutputs(): Other Format\n"));
        }
    }
}

DVDDiskNode::~DVDDiskNode()
{
    if (fInitStatus == B_OK) {
        for (uint32 i = 0; i < fOutputs.size(); i++) {
            Disconnect(fOutputs[i]->source, fOutputs[i]->destination);

            fLock.Lock();
            delete fBufferGroups[i];
            fBufferGroups[i] = NULL;
            fLock.Unlock();
        }

        if (fRunning)
            HandleStop();
    }
}

/* BDataIO */

ssize_t
DVDDiskNode::Read(void *buffer, size_t size)
{
    printf("DVD::Read(%Zu) Called\n", size);
    
	off_t curPos = Position();
	ssize_t result = ReadAt(curPos, buffer, size);
	if (result > 0)
		Seek(result, (long unsigned int) SEEK_CUR);

	return result;
}


ssize_t
DVDDiskNode::ReadAt(off_t pos, void *buffer, size_t size)
{
    printf("DVD::ReadAt(%i, %lu)\n", (int)pos, (unsigned long)size);

    int blockCount = ceil(size / DVD_VIDEO_LB_LEN);

    uint8_t *buf = (uint8_t *) buffer;
    buf = new uint8_t[blockCount * DVD_VIDEO_LB_LEN];

    for (int i = 0; i < blockCount; i++) {
        fResult = dvdnav_get_next_block(fDVDNav, buf + i * DVD_VIDEO_LB_LEN, &fEvent, &fLen);

        if (fResult == DVDNAV_STATUS_ERR) {
            printf("DVD: Error getting next block: %s\n", dvdnav_err_to_string(fDVDNav));
            return B_ERROR;
        }

        while (fEvent != DVDNAV_BLOCK_OK) {
            fResult = dvdnav_get_next_block(fDVDNav, buf + i * DVD_VIDEO_LB_LEN, &fEvent, &fLen);

            if (fResult == DVDNAV_STATUS_ERR) {
                printf("DVD: Error getting next block: %s\n", dvdnav_err_to_string(fDVDNav));
                return B_ERROR;
            }
        }
    }
    
    return blockCount * DVD_VIDEO_LB_LEN;
}


ssize_t
DVDDiskNode::WriteAt(off_t pos, const void *buffer, size_t size)
{
    // Read-only from outside
	return 0;
}


off_t
DVDDiskNode::Seek(off_t position, uint32 seekMode)
{
	switch (seekMode) {
		case SEEK_SET:
			fPosition = position;
            dvdnav_sector_search(fDVDNav, fPosition, SEEK_SET);
			break;
		case SEEK_END:
			fPosition = fLength + position;
            dvdnav_sector_search(fDVDNav, fPosition, SEEK_END);
			break;
		case SEEK_CUR:
			fPosition += position;
            dvdnav_sector_search(fDVDNav, fPosition, SEEK_CUR);
			break;
		default:
			break;
	}
	return fPosition;
}


off_t
DVDDiskNode::Position() const
{
	return fPosition;
}

/* BMediaNode */

port_id
DVDDiskNode::ControlPort() const
{
    return BMediaNode::ControlPort();
}

BMediaAddOn *
DVDDiskNode::AddOn(int32 *internal_id) const
{
    if (internal_id)
        *internal_id = fInternalID;
    return fAddOn;
}


void
DVDDiskNode::Preroll()
{
    /* This hook may be called before the node is started to give the hardware
     * a chance to start. */
}

void
DVDDiskNode::SetTimeSource(BTimeSource *time_source)
{
    /* Tell frame generation thread to recalculate delay value */
    release_sem(fStreamSync);
}

/* BMediaEventLooper */

void
DVDDiskNode::NodeRegistered()
{
    if (fInitStatus != B_OK) {
        ReportError(B_NODE_IN_DISTRESS);
        return;
    }
    
    /* Start the BMediaEventLooper control loop running */
    Run();
}

void
DVDDiskNode::Start(bigtime_t performance_time)
{
    BMediaEventLooper::Start(performance_time);
}

void
DVDDiskNode::Stop(bigtime_t performance_time, bool immediate)
{
    BMediaEventLooper::Stop(performance_time, immediate);
}

void
DVDDiskNode::Seek(bigtime_t media_time, bigtime_t performance_time)
{
    BMediaEventLooper::Seek(media_time, performance_time);
}

void
DVDDiskNode::TimeWarp(bigtime_t at_real_time, bigtime_t to_performance_time)
{
    BMediaEventLooper::TimeWarp(at_real_time, to_performance_time);
}

status_t
DVDDiskNode::AddTimer(bigtime_t at_performance_time, int32 cookie)
{
    return BMediaEventLooper::AddTimer(at_performance_time, cookie);
}

void
DVDDiskNode::SetRunMode(run_mode mode)
{
    BMediaEventLooper::SetRunMode(mode);
}

void
DVDDiskNode::HandleEvent(const media_timed_event *event,
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
DVDDiskNode::CleanUpEvent(const media_timed_event *event)
{
    BMediaEventLooper::CleanUpEvent(event);
}

bigtime_t
DVDDiskNode::OfflineTime()
{
    return BMediaEventLooper::OfflineTime();
}

void
DVDDiskNode::ControlLoop()
{
    BMediaEventLooper::ControlLoop();
}

status_t
DVDDiskNode::DeleteHook(BMediaNode * node)
{
    return BMediaEventLooper::DeleteHook(node);
}

/* BBufferProducer */

status_t
DVDDiskNode::FormatSuggestionRequested(
        media_type type, int32 quality, media_format *format)
{
    for (uint32 i = 0; i < fOutputs.size(); i++) {
        if (type == fOutputs[i]->format.type) {
            *format = fOutputs[i]->format;
            return B_OK;
        }
    }
    
    return B_MEDIA_BAD_FORMAT;
}

status_t
DVDDiskNode::FormatProposal(const media_source &output, media_format *format)
{
    status_t err;

    if (!format)
        return B_BAD_VALUE;

    for (uint32 i = 0; i < fOutputs.size(); i++) {
        if (output == fOutputs[i]->source) {
            err = format_is_compatible(*format, fOutputs[i]->format) ?
                    B_OK : B_MEDIA_BAD_FORMAT;
            *format = fOutputs[i]->format;

            return err;
        }
    }

    return B_MEDIA_BAD_SOURCE;
}

status_t
DVDDiskNode::FormatChangeRequested(const media_source &source,
        const media_destination &destination, media_format *io_format,
        int32 *_deprecated_)
{
    TOUCH(destination); TOUCH(io_format); TOUCH(_deprecated_);

    for (uint32 i = 0; i < fOutputs.size(); i++) {
        if (source == fOutputs[i]->source) {
            return B_ERROR;
        }
    }
    
    return B_MEDIA_BAD_SOURCE;
}

status_t
DVDDiskNode::GetNextOutput(int32 *cookie, media_output *out_output)
{
    if (!out_output)
        return B_BAD_VALUE;

    if ((*cookie) < 0 || (*cookie) >= fOutputs.size())
        return B_BAD_INDEX;

    *out_output = *fOutputs[(*cookie)];
    (*cookie)++;
    
    return B_OK;
}

status_t
DVDDiskNode::DisposeOutputCookie(int32 cookie)
{
    TOUCH(cookie);

    return B_OK;
}

status_t
DVDDiskNode::SetBufferGroup(const media_source &for_source,
        BBufferGroup *group)
{
    TOUCH(for_source); TOUCH(group);

    return B_ERROR;
}

status_t
DVDDiskNode::VideoClippingChanged(const media_source &for_source,
        int16 num_shorts, int16 *clip_data,
        const media_video_display_info &display, int32 *_deprecated_)
{
    TOUCH(for_source); TOUCH(num_shorts); TOUCH(clip_data);
    TOUCH(display); TOUCH(_deprecated_);

    return B_ERROR;
}

status_t
DVDDiskNode::GetLatency(bigtime_t *out_latency)
{
    *out_latency = EventLatency() + SchedulingLatency();
    return B_OK;
}

status_t
DVDDiskNode::PrepareToConnect(const media_source &source,
        const media_destination &destination, media_format *format,
        media_source *out_source, char *out_name)
{
    PRINTF(1, ("PrepareToConnect()\n")); // Fill in format details here.

    for (uint32 i = 0; i < fOutputs.size(); i++) {
        if (fOutputs[i]->source == source) {
            if (fConnected[i]) {
                PRINTF(0, ("PrepareToConnect: Output already connected\n"));
                return EALREADY;
            }

            if (fOutputs[i]->destination != media_destination::null)
                return B_MEDIA_ALREADY_CONNECTED;

            if (!format_is_compatible(*format, fOutputs[i]->format)) {
                *format = fOutputs[i]->format;
                return B_MEDIA_BAD_FORMAT;
            }

            *out_source = fOutputs[i]->source;
            strcpy(out_name, fOutputs[i]->name);

            fOutputs[i]->destination = destination;

            return B_OK;
        }
    }
    
    return B_MEDIA_BAD_SOURCE;
}

void
DVDDiskNode::Connect(status_t error, const media_source &source,
        const media_destination &destination, const media_format &format,
        char *io_name)
{
    PRINTF(1, ("Connect()\n")); // Again, complete with format details.

    for (uint32 i = 0; i < fOutputs.size(); i++) {
        if (fOutputs[i]->source == source) {
            if (fConnected[i]) {
                PRINTF(0, ("Connect: Already connected\n"));
                return;
            }

            if ((error < B_OK)
            || !const_cast<media_format *>(&format)->Matches(&fOutputs[i]->format)) {
                PRINTF(1, ("Connect: Connect error\n"));
                return;
            }

            fOutputs[i]->destination = destination;
            strcpy(io_name, fOutputs[i]->name);

            /* Tell frame generation thread to recalculate delay value */
            release_sem(fStreamSync);
            
            fConnected[i] = true;
        }
    }
}

void
DVDDiskNode::Disconnect(const media_source &source,
        const media_destination &destination)
{
    PRINTF(1, ("Disconnect()\n"));

    for (uint32 i = 0; i < fOutputs.size(); i++) {
        if (fOutputs[i]->source == source) {
            if (fOutputs[i]->destination == media_destination::null) {
                PRINTF(0, ("Disconnect: Not connected\n"));
                return;
            }

            if (destination != fOutputs[i]->destination) {
                PRINTF(0, ("Disconnect: Bad destination\n"));
                return;
            }

            fOutputs[i]->destination = media_destination::null;
            fConnected[i] = false;
        }
    }

    PRINTF(0, ("Disconnect: Bad source\n"));
    return;
}

void
DVDDiskNode::LateNoticeReceived(const media_source &source,
        bigtime_t how_much, bigtime_t performance_time)
{
    TOUCH(source); TOUCH(how_much); TOUCH(performance_time);
}

void
DVDDiskNode::EnableOutput(const media_source &source, bool enabled,
        int32 *_deprecated_)
{
    TOUCH(_deprecated_);

    return;
}

status_t
DVDDiskNode::SetPlayRate(int32 numer, int32 denom)
{
    TOUCH(numer); TOUCH(denom);

    return B_ERROR;
}

void
DVDDiskNode::AdditionalBufferRequested(const media_source &source,
        media_buffer_id prev_buffer, bigtime_t prev_time,
        const media_seek_tag *prev_tag)
{
    TOUCH(source); TOUCH(prev_buffer); TOUCH(prev_time); TOUCH(prev_tag);
}

void
DVDDiskNode::LatencyChanged(const media_source &source,
        const media_destination &destination, bigtime_t new_latency,
        uint32 flags)
{
    TOUCH(source); TOUCH(destination); TOUCH(new_latency); TOUCH(flags);
}


/* DVDDiskNode */

status_t
DVDDiskNode::HandleMessage(int32 message, const void *data, size_t size)
{
    return B_ERROR;
}


void
DVDDiskNode::HandleStart(bigtime_t performance_time)
{
    /* Start producing frames, even if the output hasn't been connected yet. */

    PRINTF(1, ("HandleStart(%Ld)\n", performance_time));

    if (fRunning) {
        PRINTF(-1, ("HandleStart: Node already started\n"));
        return;
    }

    fPerformanceTimeBase = performance_time;

    fStreamSync = create_sem(0, "stream synchronization");
    if (fStreamSync < B_OK)
        goto err1;

    fThread = spawn_thread(_stream_generator_, "stream generator",
            B_NORMAL_PRIORITY, this);
    if (fThread < B_OK)
        goto err2;

    resume_thread(fThread);

    fRunning = true;
    return;

err2:
    delete_sem(fStreamSync);
err1:
    return;
}

void
DVDDiskNode::HandleStop(void)
{
    PRINTF(1, ("HandleStop()\n"));

    if (!fRunning) {
        PRINTF(-1, ("HandleStop: Node isn't running\n"));
        return;
    }

    delete_sem(fStreamSync);
    wait_for_thread(fThread, &fThread);

    fRunning = false;
}

void
DVDDiskNode::HandleTimeWarp(bigtime_t performance_time)
{
    fPerformanceTimeBase = performance_time;

    /* Tell frame generation thread to recalculate delay value */
    release_sem(fStreamSync);
}

void
DVDDiskNode::HandleSeek(bigtime_t performance_time)
{
    fPerformanceTimeBase = performance_time;

    /* Tell frame generation thread to recalculate delay value */
    release_sem(fStreamSync);
}

/* The following functions form the thread that generates frames. You should
 * replace this with the code that interfaces to your hardware. */
int32
DVDDiskNode::StreamGenerator()
{
    bigtime_t wait_until = system_time();

    while (1) {
        status_t err = acquire_sem_etc(fStreamSync, 1, B_ABSOLUTE_TIMEOUT,
                wait_until);

        BBuffer *buffer = fBufferGroups[0]->RequestBuffer(8147200);
        if (!buffer)
            continue;
            
        /* The only acceptable responses are B_OK and B_TIMED_OUT. Everything
         * else means the thread should quit. Deleting the semaphore, as in
         * DVDDiskNode::HandleStop(), will trigger this behavior. */
        if ((err != B_OK) && (err != B_TIMED_OUT))
            break;

        /* If the semaphore was acquired successfully, it means something
         * changed the timing information (see DVDDiskNode::Connect()) and
         * so the thread should go back to sleep until the newly-calculated
         * wait_until time. */
        if (err == B_OK)
            continue;

        /* Send buffers only if the node is running and the output has been
         * enabled */
        if (!fRunning)
            continue;

        BAutolock _(fLock);

        int64 frameCount;
        PRINTF(1, ("Read frames\n"));
        fTracks[0]->ReadFrames(buffer->Data(), &frameCount, buffer->Header());
        
        PRINTF(1, ("Send Buffer\n"));
        if (SendBuffer(buffer, fOutputs[0]->source, fOutputs[0]->destination) < B_OK) {
            printf("DVD: StreamGenerator: Error sending buffer\n");
            buffer->Recycle();
        }
    }

    return B_OK;
}

int32
DVDDiskNode::_stream_generator_(void *data)
{
    return ((DVDDiskNode *)data)->StreamGenerator();
}

bool
DVDDiskNode::SetDrive(const int32 &drive)
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
DVDDiskNode::GetDrivePath() const
{
    if (!fDrivePath)
        return NULL;

    return fDrivePath->String();
}


int32
DVDDiskNode::_FindDrives(const char *path)
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
