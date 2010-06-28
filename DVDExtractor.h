#ifndef __DVDExtractorNode_H__
#define __DVDExtractorNode_H__

#include <BufferProducer.h>
#include <BufferConsumer.h>
#include <MediaEventLooper.h>

// forwards
class BBufferGroup;
class BMediaAddOn;

class DVDExtractorNode :
	public		BBufferConsumer,
	public		BBufferProducer,
	public		BMediaEventLooper {
	
public:
	virtual ~FlangerNode();
	FlangerNode(BMediaAddOn* pAddOn=0);

// Media node
	
	virtual status_t HandleMessage(
		int32 code,
		const void* pData,
		size_t size);

	virtual BMediaAddOn* AddOn(
		int32* poID) const;
	
protected:

// BMediaEventLooper

	virtual void HandleEvent(
		const media_timed_event* pEvent,
		bigtime_t howLate,
		bool realTimeEvent=false);

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
		bigtime_t* poLatency,
		media_node_id* poTimeSource);
		
	virtual status_t GetNextInput(
		int32* pioCookie,
		media_input* poInput);

	virtual void ProducerDataStatus(
		const media_destination& destination,
		int32 status,
		bigtime_t tpWhen);
	
	virtual status_t SeekTagRequested(
		const media_destination& destination,
		bigtime_t targetTime,
		uint32 flags,
		media_seek_tag* poSeekTag,
		bigtime_t* poTaggedTime,
		uint32* poFlags);
	
public:	

// BBufferProducer

	virtual void AdditionalBufferRequested(
		const media_source& source,
		media_buffer_id previousBufferID,
		bigtime_t previousTime,
		const media_seek_tag* pPreviousTag);
		
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
		media_format* pioFormat,
		int32* _deprecated_);
		
	virtual status_t FormatProposal(
		const media_source& source,
		media_format* pioFormat);
		
	virtual status_t FormatSuggestionRequested(
		media_type type,
		int32 quality,
		media_format* poFormat);
		
	virtual status_t GetLatency(
		bigtime_t* poLatency);
		
	virtual status_t GetNextOutput(
		int32* pioCookie,
		media_output* poOutput);
	
	virtual void LatencyChanged(
		const media_source& source,
		const media_destination& destination,
		bigtime_t newLatency,
		uint32 flags);

	virtual void LateNoticeReceived(
		const media_source& source,
		bigtime_t howLate,
		bigtime_t tpWhen);
	
	virtual status_t PrepareToConnect(
		const media_source& source,
		const media_destination& destination,
		media_format* pioFormat,
		media_source* poSource,
		char* poName);
		
	virtual status_t SetBufferGroup(
		const media_source& source,
		BBufferGroup* pGroup);
	
	virtual status_t SetPlayRate(
		int32 numerator,
		int32 denominator);
	
	virtual status_t VideoClippingChanged(
		const media_source& source,
		int16 numShorts,
		int16* pClipData,
		const media_video_display_info& display,
		int32* poFromChangeTag);
	
protected:

// HandleEvent() impl.

	void handleParameterEvent(
		const media_timed_event* pEvent);
		
	void handleStartEvent(
		const media_timed_event* pEvent);
		
	void handleStopEvent(
		const media_timed_event* pEvent);
		
	void ignoreEvent(
		const media_timed_event* pEvent);

protected:

// internal operations

	virtual void getPreferredFormat(
		media_format& ioFormat);
		
	status_t validateProposedFormat(
		const media_format& preferredFormat,
		media_format& ioProposedFormat);
		
	void specializeOutputFormat(
		media_format& ioFormat);
		
	// set parameters to their default settings
	virtual void initParameterValues();
	
	// create and register a parameter web
	virtual void initParameterWeb();
	
	// construct delay line if necessary, reset filter state
	virtual void initFilter();
	
	virtual void startFilter();
	virtual void stopFilter();

	// figure processing latency by doing 'dry runs' of filterBuffer()
	virtual bigtime_t calcProcessingLatency();
	
	// filter buffer data in place	
	virtual void filterBuffer(
		BBuffer* pBuffer); //nyi
		
private:

// *** connection/format members

	// The 'template' format
	// +++++ init in NodeRegistered()
	media_format			m_preferredFormat;

	// The current input/output format (this filter doesn't do any
	// on-the-fly conversion.)  Any fields that are not wildcards
	// are mandatory; the first connection (input or output) decides
	// the node's format.  If both input and output are disconnected,
	// m_format.u.raw_audio should revert to media_raw_audio_format::wildcard.	
	media_format			m_format;
	
	// Connections & associated state variables	
	media_input				m_input;

	media_output			m_output;
	bool							m_outputEnabled;

	// Time required by downstream consumer(s) to properly deliver a buffer
	bigtime_t					m_downstreamLatency;

	// Worst-case time needed to fill a buffer
	bigtime_t					m_processingLatency;

private:

// filter state

	// Frames sent since the filter started
	uint64						m_framesSent;
	
	// the buffer
	AudioBuffer*			m_pDelayBuffer;
	
	// write position (buffer offset at which the next
	// incoming frame will be stored)
	uint32						m_delayWriteFrame;
	
private:					// *** add-on stuff

	// host add-on
	BMediaAddOn*	m_pAddOn;
	
	static const char* const		s_nodeName;
};

#endif /*__DVDExtractorNode_H__*/
