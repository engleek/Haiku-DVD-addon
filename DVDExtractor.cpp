#include "DVDExtractor.h"

#include <Buffer.h>
#include <BufferGroup.h>
#include <ByteOrder.h>
#include <Debug.h>
#include <TimeSource.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>


DVDExtractor::~DVDExtractor()
{
	// shut down
	Quit();
}


DVDExtractor::DVDExtractor(BMediaAddOn* pAddOn, const char *name) :
	// base classes
	BMediaNode(name),
	BBufferConsumer(B_MEDIA_MULTISTREAM),
	BBufferProducer(B_MEDIA_RAW_AUDIO),
	BMediaEventLooper(),

	// connection state
	m_outputEnabled(true),
	m_downstreamLatency(0),
	m_processingLatency(0),

	// add-on
	m_pAddOn(pAddOn)
{
}


// -------------------------------------------------------- //
// *** BMediaNode
// -------------------------------------------------------- //

status_t
DVDExtractor::HandleMessage(
	int32 code,
	const void* pData,
	size_t size)
{
	// pass off to each base class
	if(
		BBufferConsumer::HandleMessage(code, pData, size) &&
		BBufferProducer::HandleMessage(code, pData, size) &&
		BMediaNode::HandleMessage(code, pData, size))
		BMediaNode::HandleBadMessage(code, pData, size);

	return B_OK;
}

BMediaAddOn*
DVDExtractor::AddOn(
	int32* poID) const
{

	if(m_pAddOn)
		*poID = 0;
	return m_pAddOn;
}


// -------------------------------------------------------- //
// *** BMediaEventLooper
// -------------------------------------------------------- //

void
DVDExtractor::HandleEvent(
	const media_timed_event* pEvent,
	bigtime_t howLate,
	bool realTimeEvent)
{

	ASSERT(pEvent);

	switch(pEvent->type) {
		case BTimedEventQueue::B_START:
			handleStartEvent(pEvent);
			break;

		case BTimedEventQueue::B_STOP:
			handleStopEvent(pEvent);
			break;

    case BTimedEventQueue::B_DATA_STATUS:
    case BTimedEventQueue::B_PARAMETER:
    default:
      PRINTF(-1, ("HandleEvent: Unhandled event -- %lx\n", event->type));
      break;
	}
}


void
DVDExtractor::NodeRegistered()
{
	PRINT(("DVDExtractor::NodeRegistered()\n"));

	// Start the BMediaEventLooper thread
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();

	// figure preferred ('template') format
	m_preferredFormat.type = B_MEDIA_RAW_AUDIO;
	getPreferredFormat(m_preferredFormat);

	// initialize current format
	m_format.type = B_MEDIA_RAW_AUDIO;
	m_format.u.raw_audio = media_raw_audio_format::wildcard;

	// init input
	m_input.destination.port = ControlPort();
	m_input.destination.id = ID_AUDIO_INPUT;
	m_input.node = Node();
	m_input.source = media_source::null;
	m_input.format = m_format;
	strncpy(m_input.name, "Audio Input", B_MEDIA_NAME_LENGTH);

	// init output
	m_output.source.port = ControlPort();
	m_output.source.id = ID_AUDIO_MIX_OUTPUT;
	m_output.node = Node();
	m_output.destination = media_destination::null;
	m_output.format = m_format;
	strncpy(m_output.name, "Mix Output", B_MEDIA_NAME_LENGTH);

	// init parameters
	initParameterValues();
	initParameterWeb();
}


// -------------------------------------------------------- //
// *** BBufferConsumer
// -------------------------------------------------------- //

status_t
DVDExtractor::AcceptFormat(
	const media_destination& destination,
	media_format* pioFormat)
{

	PRINT(("DVDExtractor::AcceptFormat()\n"));

	// sanity checks
	if(destination != m_input.destination) {
		PRINT(("\tbad destination\n"));
		return B_MEDIA_BAD_DESTINATION;
	}
	if(pioFormat->type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tnot B_MEDIA_RAW_AUDIO\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	validateProposedFormat(
		(m_format.u.raw_audio.format != media_raw_audio_format::wildcard.format) ?
			m_format : m_preferredFormat,
		*pioFormat);
	return B_OK;
}

// "If you're writing a node, and receive a buffer with the B_SMALL_BUFFER
//  flag set, you must recycle the buffer before returning."

void
DVDExtractor::BufferReceived(
	BBuffer* pBuffer)
{
	ASSERT(pBuffer);

	// check buffer destination
	if(pBuffer->Header()->destination !=
		m_input.destination.id) {
		PRINT(("DVDExtractor::BufferReceived():\n"
			"\tBad destination.\n"));
		pBuffer->Recycle();
		return;
	}

	if(pBuffer->Header()->time_source != TimeSource()->ID()) {
		PRINT(("* timesource mismatch\n"));
	}

	// check output
	if(m_output.destination == media_destination::null ||
		!m_outputEnabled) {
		pBuffer->Recycle();
		return;
	}

	// process and retransmit buffer
	// !!!!!!!!!!!!!!!!!!!!
  // Extractor action here!
  // !!!!!!!!!!!!!!!!!!!!

	status_t err = SendBuffer(pBuffer, m_output.source, m_output.destination);
	if (err < B_OK) {
		PRINT(("DVDExtractor::BufferReceived():\n"
			"\tSendBuffer() failed: %s\n", strerror(err)));
		pBuffer->Recycle();
	}
}


status_t
DVDExtractor::Connected(const media_source& source,
	const media_destination& destination, const media_format& format,
	media_input* poInput)
{
	PRINT(("DVDExtractor::Connected()\n"
		"\tto source %ld\n", source.id));

	// sanity check
	if(destination != m_input.destination) {
		PRINT(("\tbad destination\n"));
		return B_MEDIA_BAD_DESTINATION;
	}

	if(m_input.source != media_source::null) {
		PRINT(("\talready connected\n"));
		return B_MEDIA_ALREADY_CONNECTED;
	}

	// initialize input
	m_input.source = source;
	m_input.format = format;
	*poInput = m_input;

	// store format (this now constrains the output format)
	m_format = format;

	return B_OK;
}

void
DVDExtractor::Disconnected(
	const media_source& source,
	const media_destination& destination)
{

	PRINT(("DVDExtractor::Disconnected()\n"));

	// sanity checks
	if(m_input.source != source) {
		PRINT(("\tsource mismatch: expected ID %ld, got %ld\n",
			m_input.source.id, source.id));
		return;
	}

	if(destination != m_input.destination) {
		PRINT(("\tdestination mismatch: expected ID %ld, got %ld\n",
			m_input.destination.id, destination.id));
		return;
	}

	// mark disconnected
	m_input.source = media_source::null;

	// no output? clear format:
	if(m_output.destination == media_destination::null) {
		m_format.u.raw_audio = media_raw_audio_format::wildcard;
	}

	m_input.format = m_format;
}


void
DVDExtractor::DisposeInputCookie(
	int32 cookie)
{}


status_t
DVDExtractor::FormatChanged(
	const media_source& source,
	const media_destination& destination,
	int32 changeTag,
	const media_format& newFormat)
{

	// flat-out deny format changes
	return B_MEDIA_BAD_FORMAT;
}


status_t
DVDExtractor::GetLatencyFor(
	const media_destination& destination,
	bigtime_t* poLatency,
	media_node_id* poTimeSource)
{
	PRINT(("DVDExtractor::GetLatencyFor()\n"));

	// sanity check
	if(destination != m_input.destination) {
		PRINT(("\tbad destination\n"));
		return B_MEDIA_BAD_DESTINATION;
	}

	*poLatency = m_downstreamLatency + m_processingLatency;
	PRINT(("\treturning %Ld\n", *poLatency));
	*poTimeSource = TimeSource()->ID();
	return B_OK;
}


status_t
DVDExtractor::GetNextInput(
	int32* pioCookie,
	media_input* poInput)
{

	if(*pioCookie)
		return B_BAD_INDEX;

	++*pioCookie;
	*poInput = m_input;
	return B_OK;
}


void
DVDExtractor::ProducerDataStatus(
	const media_destination& destination,
	int32 status,
	bigtime_t tpWhen)
{
	PRINT(("DVDExtractor::ProducerDataStatus()\n"));

	// sanity check
	if(destination != m_input.destination) {
		PRINT(("\tbad destination\n"));
	}

	if(m_output.destination != media_destination::null) {
		// pass status downstream
		status_t err = SendDataStatus(
			status,
			m_output.destination,
			tpWhen);
		if(err < B_OK) {
			PRINT(("\tSendDataStatus(): %s\n", strerror(err)));
		}
	}
}


status_t
DVDExtractor::SeekTagRequested(
	const media_destination& destination,
	bigtime_t targetTime,
	uint32 flags,
	media_seek_tag* poSeekTag,
	bigtime_t* poTaggedTime,
	uint32* poFlags)
{
	PRINT(("DVDExtractor::SeekTagRequested()\n"
		"\tNot implemented.\n"));

	return B_ERROR;
}


// -------------------------------------------------------- //
// *** BBufferProducer
// -------------------------------------------------------- //

void
DVDExtractor::AdditionalBufferRequested(
	const media_source& source,
	media_buffer_id previousBufferID,
	bigtime_t previousTime,
	const media_seek_tag* pPreviousTag)
{

	PRINT(("DVDExtractor::AdditionalBufferRequested\n"
		"\tOffline mode not implemented."));
}

void
DVDExtractor::Connect(
	status_t status,
	const media_source& source,
	const media_destination& destination,
	const media_format& format,
	char* pioName)
{

	PRINT(("DVDExtractor::Connect()\n"));
	status_t err;

	// connection failed?
	if(status < B_OK) {
		PRINT(("\tStatus: %s\n", strerror(status)));
		// 'unreserve' the output
		m_output.destination = media_destination::null;
		return;
	}

	// connection established:
	strncpy(pioName, m_output.name, B_MEDIA_NAME_LENGTH);
	m_output.destination = destination;
	m_format = format;

	// figure downstream latency
	media_node_id timeSource;
	err = FindLatencyFor(m_output.destination, &m_downstreamLatency, &timeSource);
	if(err < B_OK) {
		PRINT(("\t!!! FindLatencyFor(): %s\n", strerror(err)));
	}
	PRINT(("\tdownstream latency = %Ld\n", m_downstreamLatency));

	// prepare the filter
	initFilter();

	// figure processing time
	m_processingLatency = calcProcessingLatency();
	PRINT(("\tprocessing latency = %Ld\n", m_processingLatency));

	// store summed latency
	SetEventLatency(m_downstreamLatency + m_processingLatency);

	if(m_input.source != media_source::null) {
		// pass new latency upstream
		err = SendLatencyChange(
			m_input.source,
			m_input.destination,
			EventLatency() + SchedulingLatency());
		if(err < B_OK)
			PRINT(("\t!!! SendLatencyChange(): %s\n", strerror(err)));
	}

	// cache buffer duration
	SetBufferDuration(
		buffer_duration(
			m_format.u.raw_audio));
}

void
DVDExtractor::Disconnect(
	const media_source& source,
	const media_destination& destination)
{
	PRINT(("DVDExtractor::Disconnect()\n"));

	// sanity checks
	if(source != m_output.source) {
		PRINT(("\tbad source\n"));
		return;
	}
	if(destination != m_output.destination) {
		PRINT(("\tbad destination\n"));
		return;
	}

	// clean up
	m_output.destination = media_destination::null;

	// no input? clear format:
	if(m_input.source == media_source::null) {
		m_format.u.raw_audio = media_raw_audio_format::wildcard;
	}

	m_output.format = m_format;
}

status_t
DVDExtractor::DisposeOutputCookie(
	int32 cookie)
{
	return B_OK;
}

void
DVDExtractor::EnableOutput(
	const media_source& source,
	bool enabled,
	int32* _deprecated_)
{
	PRINT(("DVDExtractor::EnableOutput()\n"));
	if(source != m_output.source) {
		PRINT(("\tbad source\n"));
		return;
	}

	m_outputEnabled = enabled;
}


status_t
DVDExtractor::FormatChangeRequested(
	const media_source& source,
	const media_destination& destination,
	media_format* pioFormat,
	int32* _deprecated_)
{

	// deny
	PRINT(("DVDExtractor::FormatChangeRequested()\n"
		"\tNot supported.\n"));

	return B_MEDIA_BAD_FORMAT;
}

status_t
DVDExtractor::FormatProposal(
	const media_source& source,
	media_format* pioFormat)
{

	PRINT(("DVDExtractor::FormatProposal()\n"));

	if(source != m_output.source) {
		PRINT(("\tbad source\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(pioFormat->type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tbad type\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	validateProposedFormat(
		(m_format.u.raw_audio.format != media_raw_audio_format::wildcard.format) ?
			m_format :
			m_preferredFormat,
		*pioFormat);
	return B_OK;
}


status_t
DVDExtractor::FormatSuggestionRequested(
	media_type type,
	int32 quality,
	media_format* poFormat)
{
	PRINT(("DVDExtractor::FormatSuggestionRequested()\n"));

	if(type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tbad type\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	if(m_format.u.raw_audio.format != media_raw_audio_format::wildcard.format)
		*poFormat = m_format;
	else
		*poFormat = m_preferredFormat;
	return B_OK;
}

status_t
DVDExtractor::GetLatency(
	bigtime_t* poLatency)
{
	PRINT(("DVDExtractor::GetLatency()\n"));
	*poLatency = EventLatency() + SchedulingLatency();
	PRINT(("\treturning %Ld\n", *poLatency));

	return B_OK;
}

status_t
DVDExtractor::GetNextOutput(
	int32* pioCookie,
	media_output* poOutput)
{
	if(*pioCookie)
		return B_BAD_INDEX;

	++*pioCookie;
	*poOutput = m_output;

	return B_OK;
}


void
DVDExtractor::LatencyChanged(
	const media_source& source,
	const media_destination& destination,
	bigtime_t newLatency,
	uint32 flags)
{
	PRINT(("DVDExtractor::LatencyChanged()\n"));

	if(source != m_output.source) {
		PRINT(("\tBad source.\n"));
		return;
	}

	if(destination != m_output.destination) {
		PRINT(("\tBad destination.\n"));
		return;
	}

	m_downstreamLatency = newLatency;
	SetEventLatency(m_downstreamLatency + m_processingLatency);

	if(m_input.source != media_source::null) {
		// pass new latency upstream
		status_t err = SendLatencyChange(
			m_input.source,
			m_input.destination,
			EventLatency() + SchedulingLatency());
		if(err < B_OK)
			PRINT(("\t!!! SendLatencyChange(): %s\n", strerror(err)));
	}
}

void
DVDExtractor::LateNoticeReceived(
	const media_source& source,
	bigtime_t howLate,
	bigtime_t tpWhen)
{
	PRINT(("DVDExtractor::LateNoticeReceived()\n"
		"\thowLate == %Ld\n"
		"\twhen    == %Ld\n", howLate, tpWhen));

	if(source != m_output.source) {
		PRINT(("\tBad source.\n"));
		return;
	}

	if(m_input.source == media_source::null) {
		PRINT(("\t!!! No input to blame.\n"));
		return;
	}

	NotifyLateProducer(
		m_input.source,
		howLate,
		tpWhen);
}


status_t
DVDExtractor::PrepareToConnect(
	const media_source& source,
	const media_destination& destination,
	media_format* pioFormat,
	media_source* poSource,
	char* poName)
{
	char formatStr[256];
	string_for_format(*pioFormat, formatStr, 255);
	PRINT(("DVDExtractor::PrepareToConnect()\n"
		"\tproposed format: %s\n", formatStr));

	if(source != m_output.source) {
		PRINT(("\tBad source.\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(m_output.destination != media_destination::null) {
		PRINT(("\tAlready connected.\n"));
		return B_MEDIA_ALREADY_CONNECTED;
	}

	if(pioFormat->type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tBad format type.\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	// do a final validity check:
	status_t err = validateProposedFormat(
		(m_format.u.raw_audio.format != media_raw_audio_format::wildcard.format) ?
			m_format :
			m_preferredFormat,
		*pioFormat);

	if(err < B_OK) {
		// no go
		return err;
	}

	// fill in wildcards
	specializeOutputFormat(*pioFormat);

	// reserve the output
	m_output.destination = destination;
	m_output.format = *pioFormat;

	// pass back source & output name
	*poSource = m_output.source;
	strncpy(poName, m_output.name, B_MEDIA_NAME_LENGTH);

	return B_OK;
}

status_t
DVDExtractor::SetBufferGroup(
	const media_source& source,
	BBufferGroup* pGroup)
{
	PRINT(("DVDExtractor::SetBufferGroup()\n"));
	if(source != m_output.source) {
		PRINT(("\tBad source.\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(m_input.source == media_source::null) {
		PRINT(("\tNo producer to send buffers to.\n"));
		return B_ERROR;
	}

	// +++++ is this right?  buffer-group selection gets
	//       all asynchronous and weird...
	int32 changeTag;
	return SetOutputBuffersFor(
		m_input.source,
		m_input.destination,
		pGroup,
		0, &changeTag);
}

status_t
DVDExtractor::SetPlayRate(
	int32 numerator,
	int32 denominator)
{
	// not supported
	return B_ERROR;
}

status_t
DVDExtractor::VideoClippingChanged(
	const media_source& source,
	int16 numShorts,
	int16* pClipData,
	const media_video_display_info& display,
	int32* poFromChangeTag)
{
	// not sane
	return B_ERROR;
}


void
DVDExtractor::handleStartEvent(
	const media_timed_event* pEvent)
{
	PRINT(("DVDExtractor::handleStartEvent\n"));

	startFilter();
}

void
DVDExtractor::handleStopEvent(
	const media_timed_event* pEvent)
{
	PRINT(("DVDExtractor::handleStopEvent\n"));

	stopFilter();
}

void
DVDExtractor::ignoreEvent(
	const media_timed_event* pEvent)
{
	PRINT(("DVDExtractor::ignoreEvent\n"));
}


// -------------------------------------------------------- //
// *** internal operations
// -------------------------------------------------------- //

void
DVDExtractor::getPreferredFormat(
	media_format& ioFormat)
{
	ASSERT(ioFormat.type == B_MEDIA_RAW_AUDIO);

	ioFormat.u.raw_audio = media_raw_audio_format::wildcard;
	ioFormat.u.raw_audio.channel_count = 1;
	ioFormat.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
}


status_t DVDExtractor::validateProposedFormat(
	const media_format& preferredFormat,
	media_format& ioProposedFormat)
{
	char formatStr[256];
	PRINT(("DVDExtractor::validateProposedFormat()\n"));

	ASSERT(preferredFormat.type == B_MEDIA_RAW_AUDIO);

	string_for_format(preferredFormat, formatStr, 255);
	PRINT(("\ttemplate format: %s\n", formatStr));

	string_for_format(ioProposedFormat, formatStr, 255);
	PRINT(("\tproposed format: %s\n", formatStr));

	status_t err = B_OK;

	if(ioProposedFormat.type != B_MEDIA_RAW_AUDIO) {
		// out of the ballpark
		ioProposedFormat = preferredFormat;
		return B_MEDIA_BAD_FORMAT;
	}

	// wildcard format
	media_raw_audio_format& wild = media_raw_audio_format::wildcard;
	// proposed format
	media_raw_audio_format& f = ioProposedFormat.u.raw_audio;
	// template format
	const media_raw_audio_format& pref = preferredFormat.u.raw_audio;

	if(pref.frame_rate != wild.frame_rate) {
		if(f.frame_rate != pref.frame_rate) {
			if(f.frame_rate != wild.frame_rate)
				err = B_MEDIA_BAD_FORMAT;
			f.frame_rate = pref.frame_rate;
		}
	}

	if(pref.channel_count != wild.channel_count) {
		if(f.channel_count != pref.channel_count) {
			if(f.channel_count != wild.channel_count)
				err = B_MEDIA_BAD_FORMAT;
			f.channel_count = pref.channel_count;
		}
	}

	if(pref.format != wild.format) {
		if(f.format != pref.format) {
			if(f.format != wild.format)
				err = B_MEDIA_BAD_FORMAT;
			f.format = pref.format;
		}
	}

	if(pref.byte_order != wild.byte_order) {
		if(f.byte_order != pref.byte_order) {
			if(f.byte_order != wild.byte_order)
				err = B_MEDIA_BAD_FORMAT;
			f.byte_order = pref.byte_order;
		}
	}

	if(pref.buffer_size != wild.buffer_size) {
		if(f.buffer_size != pref.buffer_size) {
			if(f.buffer_size != wild.buffer_size)
				err = B_MEDIA_BAD_FORMAT;
			f.buffer_size = pref.buffer_size;
		}
	}

	if(err != B_OK) {
		string_for_format(ioProposedFormat, formatStr, 255);
		PRINT((
			"\tformat conflict; suggesting:\n\tformat %s\n", formatStr));
	}

	return err;
}


void
DVDExtractor::specializeOutputFormat(
	media_format& ioFormat)
{
	char formatStr[256];
	string_for_format(ioFormat, formatStr, 255);
	PRINT(("DVDExtractor::specializeOutputFormat()\n"
		"\tinput format: %s\n", formatStr));

	ASSERT(ioFormat.type == B_MEDIA_RAW_AUDIO);

	// carpal_tunnel_paranoia
	media_raw_audio_format& f = ioFormat.u.raw_audio;
	media_raw_audio_format& w = media_raw_audio_format::wildcard;

	if(f.frame_rate == w.frame_rate)
		f.frame_rate = 44100.0;
	if(f.channel_count == w.channel_count) {
		//+++++ tweaked 15sep99
		if(m_input.source != media_source::null)
			f.channel_count = m_input.format.u.raw_audio.channel_count;
		else
			f.channel_count = 1;
	}
	if(f.format == w.format)
		f.format = media_raw_audio_format::B_AUDIO_FLOAT;
	if(f.byte_order == w.format)
		f.byte_order = (B_HOST_IS_BENDIAN) ? B_MEDIA_BIG_ENDIAN : B_MEDIA_LITTLE_ENDIAN;
	if(f.buffer_size == w.buffer_size)
		f.buffer_size = 2048;

	string_for_format(ioFormat, formatStr, 255);
	PRINT(("\toutput format: %s\n", formatStr));
}


// construct delay line if necessary, reset filter state
void
DVDExtractor::initFilter()
{
	PRINT(("DVDExtractor::initFilter()\n"));
	ASSERT(m_format.u.raw_audio.format != media_raw_audio_format::wildcard.format);

	m_framesSent = 0;
	m_delayWriteFrame = 0;
}


void
DVDExtractor::startFilter()
{
	PRINT(("DVDExtractor::startFilter()\n"));
}


void
DVDExtractor::stopFilter()
{
	PRINT(("DVDExtractor::stopFilter()\n"));
}


// figure processing latency by doing 'dry runs' of filterBuffer()
bigtime_t
DVDExtractor::calcProcessingLatency()
{
	PRINT(("DVDExtractor::calcProcessingLatency()\n"));

	if(m_output.destination == media_destination::null) {
		PRINT(("\tNot connected.\n"));
		return 0LL;
	}

	// allocate a temporary buffer group
	BBufferGroup* pTestGroup = new BBufferGroup(
		m_output.format.u.raw_audio.buffer_size, 1);

	// fetch a buffer
	BBuffer* pBuffer = pTestGroup->RequestBuffer(
		m_output.format.u.raw_audio.buffer_size);
	ASSERT(pBuffer);

	pBuffer->Header()->type = B_MEDIA_RAW_AUDIO;
	pBuffer->Header()->size_used = m_output.format.u.raw_audio.buffer_size;

	// run the test
	bigtime_t preTest = system_time();
	filterBuffer(pBuffer);
	bigtime_t elapsed = system_time()-preTest;

	// clean up
	pBuffer->Recycle();
	delete pTestGroup;

	// reset filter state
	initFilter();

	return elapsed;
}


void DVDExtractor::filterBuffer(
	BBuffer* pBuffer)
{
	if(!m_pDelayBuffer)
		return;

	ASSERT(
		m_format.u.raw_audio.channel_count == 1 ||
		m_format.u.raw_audio.channel_count == 2);
	uint32 channels = m_format.u.raw_audio.channel_count;
	bool stereo = m_format.u.raw_audio.channel_count == 2;

	uint32 samples = input.frames() * channels;
	for(uint32 inPos = 0; inPos < samples; ++inPos) {

		// read from input buffer
		_frame inFrame;
		inFrame.channel[0] = ((float*)input.data())[inPos];
		if(stereo)
			inFrame.channel[1] = ((float*)input.data())[inPos + 1];

		// interpolate from delay buffer
		float readOffset = m_fSweepBase + (m_fSweepFactor * sin(m_fTheta));
		float fReadFrame = (float)m_delayWriteFrame - readOffset;
		if(fReadFrame < 0.0)
			fReadFrame += m_pDelayBuffer->frames();

		// read low-index (possibly only) frame
		_frame delayedFrame;

		int32 readFrameLo = (int32)floor(fReadFrame);
		uint32 pos = readFrameLo * channels;
		delayedFrame.channel[0] = ((float*)m_pDelayBuffer->data())[pos];
		if(stereo)
			delayedFrame.channel[1] = ((float*)m_pDelayBuffer->data())[pos+1];

		if(readFrameLo != (int32)fReadFrame) {

			// interpolate (A)
			uint32 readFrameHi = (int32)ceil(fReadFrame);
			delayedFrame.channel[0] *= ((float)readFrameHi - fReadFrame);
			if(stereo)
				delayedFrame.channel[1] *= ((float)readFrameHi - fReadFrame);

			// read high-index frame
			int32 hiWrap = (readFrameHi == m_pDelayBuffer->frames()) ? 0 : readFrameHi;
			ASSERT(hiWrap >= 0);
			pos = (uint32)hiWrap * channels;
			_frame hiFrame;
			hiFrame.channel[0] = ((float*)m_pDelayBuffer->data())[pos];
			if(stereo)
				hiFrame.channel[1] = ((float*)m_pDelayBuffer->data())[pos+1];

			// interpolate (B)
			delayedFrame.channel[0] +=
				hiFrame.channel[0] * (fReadFrame - (float)readFrameLo);
			if(stereo)
				delayedFrame.channel[1] +=
					hiFrame.channel[1] * (fReadFrame - (float)readFrameLo);
		}

		// mix back to output buffer
		((float*)input.data())[inPos] =
			(inFrame.channel[0] * (1.0-m_fMixRatio)) +
			(delayedFrame.channel[0] * m_fMixRatio);
		if(stereo)
			((float*)input.data())[inPos+1] =
				(inFrame.channel[1] * (1.0-m_fMixRatio)) +
				(delayedFrame.channel[1] * m_fMixRatio);

		// write to delay buffer
		uint32 delayWritePos = m_delayWriteFrame * channels;
		((float*)m_pDelayBuffer->data())[delayWritePos] =
			inFrame.channel[0] +
			(delayedFrame.channel[0] * m_fFeedback);
		if(stereo)
			((float*)m_pDelayBuffer->data())[delayWritePos+1] =
				inFrame.channel[1] +
				(delayedFrame.channel[1] * m_fFeedback);

		// advance write position
		if(++m_delayWriteFrame >= m_pDelayBuffer->frames())
			m_delayWriteFrame = 0;

		// advance read offset ('LFO')
		m_fTheta += m_fThetaInc;
		if(m_fTheta > 2 * M_PI)
			m_fTheta -= 2 * M_PI;
	}
}
