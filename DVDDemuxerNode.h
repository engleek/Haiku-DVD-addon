#ifndef __DVDDemuxerNode_H__
#define __DVDDemuxerNode_H__

#include <Buffer.h>
#include <BufferConsumer.h>
#include <BufferGroup.h>
#include <BufferProducer.h>
#include <MediaEventLooper.h>
#include <MediaDefs.h>
#include <MediaNode.h>
#include <MediaAddOn.h>
#include <MediaExtractor.h>

#include <vector>

class DVDDemuxerNode :
	public BBufferConsumer,
	public BBufferProducer,
	public BMediaEventLooper
{
	
public:
	virtual ~DVDDemuxerNode();

	DVDDemuxerNode(
		const flavor_info *info = 0,
		BMediaAddOn *addOn = 0);

	virtual status_t InitCheck(void) const;
	
	virtual BMediaAddOn* AddOn(
		int32* ID) const;
	
	virtual status_t HandleMessage(
		int32 code,
		const void* data,
		size_t size);
	
protected:

// BMediaEventLooper

	virtual void HandleEvent(
		const media_timed_event* event,
		bigtime_t howLate,
		bool realTimeEvent = false);

protected:

	virtual void NodeRegistered();
	virtual bigtime_t OfflineTime();

public:					

// BBufferConsumer

	virtual status_t AcceptFormat(
		const media_destination& destination,
		media_format* pioFormat);
	
	virtual void BufferReceived(
		BBuffer* pBuffer);

	virtual status_t Connected(
		const media_source& source,
		const media_destination& destination,
		const media_format& format,
		media_input* poInput);

	virtual void Disconnected(
		const media_source& source,
		const media_destination& destination);
		
	virtual void DisposeInputCookie(
		int32 cookie);
	
	virtual status_t FormatChanged(
		const media_source& source,
		const media_destination& destination,
		int32 changeTag,
		const media_format& newFormat);
		
	virtual status_t GetLatencyFor(
		const media_destination& destination,
		bigtime_t* latency,
		media_node_id* timeSource);
		
	virtual status_t GetNextInput(
		int32* cookie,
		media_input* input);

	virtual void ProducerDataStatus(
		const media_destination& destination,
		int32 status,
		bigtime_t when);
	
	virtual status_t SeekTagRequested(
		const media_destination& destination,
		bigtime_t targetTime,
		uint32 in_flags,
		media_seek_tag* seekTag,
		bigtime_t* taggedTime,
		uint32* out_flags);
	
public:	

// BBufferProducer

	virtual void AdditionalBufferRequested(
		const media_source& source,
		media_buffer_id previousBufferID,
		bigtime_t previousTime,
		const media_seek_tag* previousTag);
		
	virtual void Connect(
		status_t status,
		const media_source& source,
		const media_destination& destination,
		const media_format& format,
		char* pioName);
		
	virtual void Disconnect(
		const media_source& source,
		const media_destination& destination);
		
	virtual status_t DisposeOutputCookie(
		int32 cookie);
		
	virtual void EnableOutput(
		const media_source& source,
		bool enabled,
		int32* _deprecated_);
		
	virtual status_t FormatChangeRequested(
		const media_source& source,
		const media_destination& destination,
		media_format* format,
		int32* _deprecated_);
		
	virtual status_t FormatProposal(
		const media_source& source,
		media_format* pioFormat);
		
	virtual status_t FormatSuggestionRequested(
		media_type type,
		int32 quality,
		media_format* format);
		
	virtual status_t GetLatency(
		bigtime_t* latency);
		
	virtual status_t GetNextOutput(
		int32* cookie,
		media_output* output);
	
	virtual void LatencyChanged(
		const media_source& source,
		const media_destination& destination,
		bigtime_t newLatency,
		uint32 flags);

	virtual void LateNoticeReceived(
		const media_source& source,
		bigtime_t howLate,
		bigtime_t when);
	
	virtual status_t PrepareToConnect(
		const media_source& source,
		const media_destination& destination,
		media_format* format,
		media_source* source,
		char* name);
		
	virtual status_t SetBufferGroup(
		const media_source& source,
		BBufferGroup* group);
	
	virtual status_t SetPlayRate(
		int32 numerator,
		int32 denominator);
	
	virtual status_t VideoClippingChanged(
		const media_source& source,
		int16 numShorts,
		int16* clipData,
		const media_video_display_info& display,
		int32* fromChangeTag);
	
protected:

// HandleEvent() impl.

	void handleParameter(
		const media_timed_event* event);
		
	void handleStart(
		const media_timed_event* event);
		
	void handleStop(
		const media_timed_event* event);
		
	void ignoreEvent(
		const media_timed_event* event);

protected:

// internal operations

	virtual void getPreferredFormat(
		media_format& format);
		
	status_t validateProposedFormat(
		const media_format& preferredFormat,
		media_format& proposedFormat);
		
	void specializeOutputFormat(
		media_format& format);
			
	virtual void start();
	virtual void stop();

	// figure processing latency by doing 'dry runs' of filterBuffer()
	virtual bigtime_t calcProcessingLatency();
	
	// filter buffer data in place	
	virtual void buffer(
		BBuffer* buffer); //nyi
		
private:

// *** connection/format members

	// The 'template' format
	// +++++ init in NodeRegistered()
	media_format			fPreferredFormat;

	// The current input/output format (this filter doesn't do any
	// on-the-fly conversion.)  Any fields that are not wildcards
	// are mandatory; the first connection (input or output) decides
	// the node's format.  If both input and output are disconnected,
	// m_format.u.raw_audio should revert to media_raw_audio_format::wildcard.	
	media_format			fFormat;
	
	flavor_info				fFlavorInfo;
	
	// Connections & associated state variables	
	media_input				fInput;

	media_output			fOutputs[3];
	bool					fOutputsEnabled;

	// Time required by downstream consumer(s) to properly deliver a buffer
	bigtime_t				fDownstreamLatency;

	// Worst-case time needed to fill a buffer
	bigtime_t				fProcessingLatency;
	
	status_t fInitCheckStatus;
	
private:					// *** add-on stuff

	// host add-on
	BMediaAddOn	*fAddOn;
	
	//static const char* const		s_nodeName;
};

#endif /*__DVDDemuxerNode_H__*/
