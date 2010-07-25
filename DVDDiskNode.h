#ifndef DVD_MEDIA_NODE_H
#define DVD_MEDIA_NODE_H

#include "scsi.h"

#include <kernel/OS.h>
#include <media/BufferProducer.h>
#include <media/Controllable.h>
#include <media/MediaDefs.h>
#include <media/MediaEventLooper.h>
#include <media/MediaNode.h>
#include <media/MediaDecoder.h>
#include <support/Locker.h>
#include <String.h>
#include <DataIO.h>
#include <MediaFile.h>
#include <MediaTrack.h>

#include <vector>

#include "dvdnav.h"

class DVDDiskNode :
    public virtual BPositionIO,
    public virtual BMediaEventLooper,
    public virtual BBufferProducer
{
public:
                    DVDDiskNode(BMediaAddOn *addon,
                                const char *name, int32 internal_id);
virtual             ~DVDDiskNode();

virtual status_t    InitCheck() const { return fInitStatus; }

/* BPositionIO */

ssize_t Read(void *buffer, size_t size);
ssize_t ReadAt(off_t position, void *buffer, size_t size);
ssize_t WriteAt(off_t position, const void *buffer,
                    size_t size);

off_t Seek(off_t position, uint32 seekMode);
off_t Position() const;

/* BMediaNode */

public:
virtual port_id     ControlPort() const;
virtual BMediaAddOn *AddOn(int32 * internal_id) const;
virtual status_t    HandleMessage(int32 message, const void *data,
                            size_t size);
protected:
virtual void        Preroll();
virtual void        SetTimeSource(BTimeSource * time_source);


/* BMediaEventLooper */

protected:
virtual void        NodeRegistered();
virtual void        Start(bigtime_t performance_time);
virtual void        Stop(bigtime_t performance_time, bool immediate);
virtual void        Seek(bigtime_t media_time, bigtime_t performance_time);
virtual void        TimeWarp(bigtime_t at_real_time,
                            bigtime_t to_performance_time);
virtual status_t    AddTimer(bigtime_t at_performance_time, int32 cookie);
virtual void        SetRunMode(run_mode mode);
virtual void        HandleEvent(const media_timed_event *event,
                            bigtime_t lateness, bool realTimeEvent = false);
virtual void        CleanUpEvent(const media_timed_event *event);
virtual bigtime_t   OfflineTime();
virtual void        ControlLoop();
virtual status_t    DeleteHook(BMediaNode * node);


/* BBufferProducer */

protected:
virtual status_t    FormatSuggestionRequested(media_type type, int32 quality,
                            media_format * format);
virtual status_t    FormatProposal(const media_source &output,
                            media_format *format);
virtual status_t    FormatChangeRequested(const media_source &source,
                            const media_destination &destination,
                            media_format *io_format, int32 *_deprecated_);
virtual status_t    GetNextOutput(int32 * cookie, media_output * out_output);
virtual status_t    DisposeOutputCookie(int32 cookie);
virtual status_t    SetBufferGroup(const media_source &for_source,
                            BBufferGroup * group);
virtual status_t    VideoClippingChanged(const media_source &for_source,
                            int16 num_shorts, int16 *clip_data,
                            const media_video_display_info &display,
                            int32 * _deprecated_);
virtual status_t    GetLatency(bigtime_t * out_latency);
virtual status_t    PrepareToConnect(const media_source &what,
                            const media_destination &where,
                            media_format *format,
                            media_source *out_source, char *out_name);
virtual void        Connect(status_t error, const media_source &source,
                            const media_destination &destination,
                            const media_format & format, char *io_name);
virtual void        Disconnect(const media_source & what,
                            const media_destination & where);
virtual void        LateNoticeReceived(const media_source & what,
                            bigtime_t how_much, bigtime_t performance_time);
virtual void        EnableOutput(const media_source & what, bool enabled,
                            int32 * _deprecated_);
virtual status_t    SetPlayRate(int32 numer,int32 denom);
virtual void        AdditionalBufferRequested(const media_source & source,
                            media_buffer_id prev_buffer, bigtime_t prev_time,
                            const media_seek_tag * prev_tag);
virtual void        LatencyChanged(const media_source & source,
                            const media_destination & destination,
                            bigtime_t new_latency, uint32 flags);

private:
        void        HandleStart(bigtime_t performance_time);
        void        HandleStop();
        void        HandleTimeWarp(bigtime_t performance_time);
        void        HandleSeek(bigtime_t performance_time);

        // Drive functions, remove eventually
        bool        SetDrive(const int32 &drive);
        const       char* GetDrivePath() const;
        int32       _FindDrives(const char *path);

        void        LoadDisk();
        void        InitOutputs();

        status_t            fInitStatus;

        int32               fInternalID;
        BMediaAddOn         *fAddOn;

        BLocker             fLock;

        thread_id           fThread;
        sem_id              fStreamSync;
static  int32               _stream_generator_(void *data);
        int32               StreamGenerator();

		std::vector<media_output *>	fOutputs;
        std::vector<BBufferGroup *> fBufferGroups;
        std::vector<BMediaTrack *>  fTracks;
        std::vector<bool>           fConnected;

        bigtime_t           fPerformanceTimeBase;
        bigtime_t           fProcessingLatency;
        bool                fRunning;
        bool                fDVDLoaded;

        BList               fDriveList;
        BString*            fDrivePath;
        int32               fDriveIndex;

        dvdnav_t            *fDVDNav;
        BMediaFile          *fMediaFile;
        
        int                 fResult;
        int                 fEvent;
        int                 fLen;
        uint8_t             *fBuffer;
        int                 fBufferSize;
		size_t	            fPosition;
		size_t	            fLength;
};

#endif
