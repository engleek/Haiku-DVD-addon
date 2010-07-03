#include "DVDExtractorNode.h"

#include <Buffer.h>
#include <BufferGroup.h>
#include <ByteOrder.h>
#include <Debug.h>
#include <TimeSource.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#define PRINTF(a,b) \
        do { \
            if (a < 2) { \
                printf("DVDMediaNode::"); \
                printf b; \
            } \
        } while (0)

DVDExtractorNode::~DVDExtractorNode()
{
	// shut down
	Quit();
}


DVDExtractorNode::DVDExtractorNode(const flavor_info *info, BMediaAddOn* addOn) :
	// base classes
	BMediaNode("DVD Extractor Node"),
	BBufferConsumer(B_MEDIA_MULTISTREAM),
	BBufferProducer(B_MEDIA_UNKNOWN_TYPE),
	BMediaEventLooper(),

	// connection state
	fOutputEnabled(true),
	fDownstreamLatency(0),
	fProcessingLatency(0),

	// add-on
	fAddOn(addOn)
{
}


// -------------------------------------------------------- //
// *** BMediaNode
// -------------------------------------------------------- //

status_t
DVDExtractorNode::HandleMessage(
	int32 code,
	const void* data,
	size_t size)
{
	// pass off to each base class
	if(
		BBufferConsumer::HandleMessage(code, data, size) &&
		BBufferProducer::HandleMessage(code, data, size) &&
		BMediaNode::HandleMessage(code, data, size))
		BMediaNode::HandleBadMessage(code, data, size);

	return B_OK;
}

BMediaAddOn*
DVDExtractorNode::AddOn(
	int32* ID) const
{

	if(fAddOn)
		*ID = 0;
	return fAddOn;
}


status_t
DVDExtractorNode::InitCheck(void) const
{
	fprintf(stderr,"DVDExtractorNode::InitCheck\n");
	return fInitCheckStatus;
}


// -------------------------------------------------------- //
// *** BMediaEventLooper
// -------------------------------------------------------- //

void
DVDExtractorNode::HandleEvent(
	const media_timed_event* event,
	bigtime_t howLate,
	bool realTimeEvent)
{

	ASSERT(event);

	switch(event->type) {
		case BTimedEventQueue::B_START:
			handleStartEvent(event);
			break;

		case BTimedEventQueue::B_STOP:
			handleStopEvent(event);
			break;

    case BTimedEventQueue::B_DATA_STATUS:
    case BTimedEventQueue::B_PARAMETER:
    default:
      PRINTF(-1, ("HandleEvent: Unhandled event -- %lx\n", event->type));
      break;
	}
}


void
DVDExtractorNode::NodeRegistered()
{
	PRINT(("DVDExtractorNode::NodeRegistered()\n"));

	// Start the BMediaEventLooper thread
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
/*
	// figure preferred ('template') format
	fPreferredFormat.type = B_MEDIA_RAW_AUDIO;
	getPreferredFormat(fPreferredFormat);

	// initialize current format
	fFormat.type = B_MEDIA_RAW_AUDIO;
	fFormat.u.raw_audio = media_raw_audio_format::wildcard;

	// init input
	fInput.destination.port = ControlPort();
	fInput.destination.id = ID_AUDIO_INPUT;
	fInput.node = Node();
	fInput.source = media_source::null;
	fInput.format = fFormat;
	strncpy(fInput.name, "Audio Input", B_MEDIA_NAME_LENGTH);

	// init output
	fOutput.source.port = ControlPort();
	fOutput.source.id = ID_AUDIO_MIX_OUTPUT;
	fOutput.node = Node();
	fOutput.destination = media_destination::null;
	fOutput.format = fFormat;
	strncpy(fOutput.name, "Mix Output", B_MEDIA_NAME_LENGTH);

	// init parameters
	initParameterValues();
	initParameterWeb();
*/
}


bigtime_t DVDExtractorNode::OfflineTime()
{
	fprintf(stderr,"DVDExtractorNode(BMediaEventLooper)::OfflineTime\n");
	return BMediaEventLooper::OfflineTime();
}


// -------------------------------------------------------- //
// *** BBufferConsumer
// -------------------------------------------------------- //

status_t
DVDExtractorNode::AcceptFormat(
	const media_destination& destination,
	media_format* format)
{

	PRINT(("DVDExtractorNode::AcceptFormat()\n"));

	// sanity checks
	if(destination != fInput.destination) {
		PRINT(("\tbad destination\n"));
		return B_MEDIA_BAD_DESTINATION;
	}
	if(format->type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tnot B_MEDIA_RAW_AUDIO\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	validateProposedFormat(
		(fFormat.u.raw_audio.format != media_raw_audio_format::wildcard.format) ?
			fFormat : fPreferredFormat,
		*format);
	return B_OK;
}

// "If you're writing a node, and receive a buffer with the B_SMALL_BUFFER
//  flag set, you must recycle the buffer before returning."

void
DVDExtractorNode::BufferReceived(
	BBuffer* buffer)
{
	ASSERT(buffer);

	// check buffer destination
	if(buffer->Header()->destination !=
		fInput.destination.id) {
		PRINT(("DVDExtractorNode::BufferReceived():\n"
			"\tBad destination.\n"));
		buffer->Recycle();
		return;
	}

	if(buffer->Header()->time_source != TimeSource()->ID()) {
		PRINT(("* timesource mismatch\n"));
	}

	// check output
	if(fOutput.destination == media_destination::null ||
		!fOutputEnabled) {
		buffer->Recycle();
		return;
	}

	// process and retransmit buffer
	// !!!!!!!!!!!!!!!!!!!!
  // Extractor action here!
  // !!!!!!!!!!!!!!!!!!!!

	status_t err = SendBuffer(buffer, fOutput.source, fOutput.destination);
	if (err < B_OK) {
		PRINT(("DVDExtractorNode::BufferReceived():\n"
			"\tSendBuffer() failed: %s\n", strerror(err)));
		buffer->Recycle();
	}
}


status_t
DVDExtractorNode::Connected(const media_source& source,
	const media_destination& destination, const media_format& format,
	media_input* input)
{
	PRINT(("DVDExtractorNode::Connected()\n"
		"\tto source %ld\n", source.id));

	// sanity check
	if(destination != fInput.destination) {
		PRINT(("\tbad destination\n"));
		return B_MEDIA_BAD_DESTINATION;
	}

	if(fInput.source != media_source::null) {
		PRINT(("\talready connected\n"));
		return B_MEDIA_ALREADY_CONNECTED;
	}

	// initialize input
	fInput.source = source;
	fInput.format = format;
	*input = fInput;

	// store format (this now constrains the output format)
	fFormat = format;

	return B_OK;
}

void
DVDExtractorNode::Disconnected(
	const media_source& source,
	const media_destination& destination)
{

	PRINT(("DVDExtractorNode::Disconnected()\n"));

	// sanity checks
	if(fInput.source != source) {
		PRINT(("\tsource mismatch: expected ID %ld, got %ld\n",
			fInput.source.id, source.id));
		return;
	}

	if(destination != fInput.destination) {
		PRINT(("\tdestination mismatch: expected ID %ld, got %ld\n",
			fInput.destination.id, destination.id));
		return;
	}

	// mark disconnected
	fInput.source = media_source::null;

	// no output? clear format:
	if(fOutput.destination == media_destination::null) {
		fFormat.u.raw_audio = media_raw_audio_format::wildcard;
	}

	fInput.format = fFormat;
}


void
DVDExtractorNode::DisposeInputCookie(
	int32 cookie)
{}


status_t
DVDExtractorNode::FormatChanged(
	const media_source& source,
	const media_destination& destination,
	int32 changeTag,
	const media_format& newFormat)
{
	// flat-out deny format changes
	return B_MEDIA_BAD_FORMAT;
}


status_t
DVDExtractorNode::GetLatencyFor(
	const media_destination& destination,
	bigtime_t* latency,
	media_node_id* timeSource)
{
	PRINT(("DVDExtractorNode::GetLatencyFor()\n"));

	// sanity check
	if(destination != fInput.destination) {
		PRINT(("\tbad destination\n"));
		return B_MEDIA_BAD_DESTINATION;
	}

	*latency = fDownstreamLatency + fProcessingLatency;
	PRINT(("\treturning %Ld\n", *latency));
	*timeSource = TimeSource()->ID();
	return B_OK;
}


status_t
DVDExtractorNode::GetNextInput(
	int32* cookie,
	media_input* input)
{

	if(*cookie)
		return B_BAD_INDEX;

	++*cookie;
	*input = fInput;
	return B_OK;
}


void
DVDExtractorNode::ProducerDataStatus(
	const media_destination& destination,
	int32 status,
	bigtime_t when)
{
	PRINT(("DVDExtractorNode::ProducerDataStatus()\n"));

	// sanity check
	if(destination != fInput.destination) {
		PRINT(("\tbad destination\n"));
	}

	if(fOutput.destination != media_destination::null) {
		// pass status downstream
		status_t err = SendDataStatus(
			status,
			fOutput.destination,
			when);
		if(err < B_OK) {
			PRINT(("\tSendDataStatus(): %s\n", strerror(err)));
		}
	}
}


status_t
DVDExtractorNode::SeekTagRequested(
	const media_destination& destination,
	bigtime_t targetTime,
	uint32 in_flags,
	media_seek_tag* seekTag,
	bigtime_t* taggedTime,
	uint32* out_flags)
{
	PRINT(("DVDExtractorNode::SeekTagRequested()\n"
		"\tNot implemented.\n"));

	return B_ERROR;
}


// -------------------------------------------------------- //
// *** BBufferProducer
// -------------------------------------------------------- //

void
DVDExtractorNode::AdditionalBufferRequested(
	const media_source& source,
	media_buffer_id previousBufferID,
	bigtime_t previousTime,
	const media_seek_tag* previousTag)
{

	PRINT(("DVDExtractorNode::AdditionalBufferRequested\n"
		"\tOffline mode not implemented."));
}

void
DVDExtractorNode::Connect(
	status_t status,
	const media_source& source,
	const media_destination& destination,
	const media_format& format,
	char* name)
{

	PRINT(("DVDExtractorNode::Connect()\n"));
	status_t err;

	// connection failed?
	if(status < B_OK) {
		PRINT(("\tStatus: %s\n", strerror(status)));
		// 'unreserve' the output
		fOutput.destination = media_destination::null;
		return;
	}

	// connection established:
	strncpy(name, fOutput.name, B_MEDIA_NAME_LENGTH);
	fOutput.destination = destination;
	fFormat = format;

	// figure downstream latency
	media_node_id timeSource;
	err = FindLatencyFor(fOutput.destination, &fDownstreamLatency, &timeSource);
	if(err < B_OK) {
		PRINT(("\t!!! FindLatencyFor(): %s\n", strerror(err)));
	}
	PRINT(("\tdownstream latency = %Ld\n", fDownstreamLatency));

	// prepare the filter
	initFilter();

	// figure processing time
	fProcessingLatency = calcProcessingLatency();
	PRINT(("\tprocessing latency = %Ld\n", fProcessingLatency));

	// store summed latency
	SetEventLatency(fDownstreamLatency + fProcessingLatency);

	if(fInput.source != media_source::null) {
		// pass new latency upstream
		err = SendLatencyChange(
			fInput.source,
			fInput.destination,
			EventLatency() + SchedulingLatency());
		if(err < B_OK)
			PRINT(("\t!!! SendLatencyChange(): %s\n", strerror(err)));
	}
}

void
DVDExtractorNode::Disconnect(
	const media_source& source,
	const media_destination& destination)
{
	PRINT(("DVDExtractorNode::Disconnect()\n"));

	// sanity checks
	if(source != fOutput.source) {
		PRINT(("\tbad source\n"));
		return;
	}
	if(destination != fOutput.destination) {
		PRINT(("\tbad destination\n"));
		return;
	}

	// clean up
	fOutput.destination = media_destination::null;

	// no input? clear format:
	if(fInput.source == media_source::null) {
		fFormat.u.raw_audio = media_raw_audio_format::wildcard;
	}

	fOutput.format = fFormat;
}

status_t
DVDExtractorNode::DisposeOutputCookie(
	int32 cookie)
{
	return B_OK;
}

void
DVDExtractorNode::EnableOutput(
	const media_source& source,
	bool enabled,
	int32* _deprecated_)
{
	PRINT(("DVDExtractorNode::EnableOutput()\n"));
	if(source != fOutput.source) {
		PRINT(("\tbad source\n"));
		return;
	}

	fOutputEnabled = enabled;
}


status_t
DVDExtractorNode::FormatChangeRequested(
	const media_source& source,
	const media_destination& destination,
	media_format* format,
	int32* _deprecated_)
{

	// deny
	PRINT(("DVDExtractorNode::FormatChangeRequested()\n"
		"\tNot supported.\n"));

	return B_MEDIA_BAD_FORMAT;
}

status_t
DVDExtractorNode::FormatProposal(
	const media_source& source,
	media_format* format)
{

	PRINT(("DVDExtractorNode::FormatProposal()\n"));

	if(source != fOutput.source) {
		PRINT(("\tbad source\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(format->type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tbad type\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	validateProposedFormat(
		(fFormat.u.raw_audio.format != media_raw_audio_format::wildcard.format) ?
			fFormat :
			fPreferredFormat,
		*format);
	return B_OK;
}


status_t
DVDExtractorNode::FormatSuggestionRequested(
	media_type type,
	int32 quality,
	media_format* format)
{
	PRINT(("DVDExtractorNode::FormatSuggestionRequested()\n"));

	if(type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tbad type\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	if(fFormat.u.raw_audio.format != media_raw_audio_format::wildcard.format)
		*format = fFormat;
	else
		*format = fPreferredFormat;
	return B_OK;
}

status_t
DVDExtractorNode::GetLatency(
	bigtime_t* latency)
{
	PRINT(("DVDExtractorNode::GetLatency()\n"));
	*latency = EventLatency() + SchedulingLatency();
	PRINT(("\treturning %Ld\n", *latency));

	return B_OK;
}

status_t
DVDExtractorNode::GetNextOutput(
	int32* cookie,
	media_output* output)
{
	if(*cookie)
		return B_BAD_INDEX;

	++*cookie;
	*output = fOutput;

	return B_OK;
}


void
DVDExtractorNode::LatencyChanged(
	const media_source& source,
	const media_destination& destination,
	bigtime_t newLatency,
	uint32 flags)
{
	PRINT(("DVDExtractorNode::LatencyChanged()\n"));

	if(source != fOutput.source) {
		PRINT(("\tBad source.\n"));
		return;
	}

	if(destination != fOutput.destination) {
		PRINT(("\tBad destination.\n"));
		return;
	}

	fDownstreamLatency = newLatency;
	SetEventLatency(fDownstreamLatency + fProcessingLatency);

	if(fInput.source != media_source::null) {
		// pass new latency upstream
		status_t err = SendLatencyChange(
			fInput.source,
			fInput.destination,
			EventLatency() + SchedulingLatency());
		if(err < B_OK)
			PRINT(("\t!!! SendLatencyChange(): %s\n", strerror(err)));
	}
}

void
DVDExtractorNode::LateNoticeReceived(
	const media_source& source,
	bigtime_t howLate,
	bigtime_t when)
{
	PRINT(("DVDExtractorNode::LateNoticeReceived()\n"
		"\thowLate == %Ld\n"
		"\twhen    == %Ld\n", howLate, when));

	if(source != fOutput.source) {
		PRINT(("\tBad source.\n"));
		return;
	}

	if(fInput.source == media_source::null) {
		PRINT(("\t!!! No input to blame.\n"));
		return;
	}

	NotifyLateProducer(
		fInput.source,
		howLate,
		when);
}


status_t
DVDExtractorNode::PrepareToConnect(
	const media_source& source,
	const media_destination& destination,
	media_format* format,
	media_source* out_source,
	char* name)
{
	char formatStr[256];
	string_for_format(*format, formatStr, 255);
	PRINT(("DVDExtractorNode::PrepareToConnect()\n"
		"\tproposed format: %s\n", formatStr));

	if(source != fOutput.source) {
		PRINT(("\tBad source.\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(fOutput.destination != media_destination::null) {
		PRINT(("\tAlready connected.\n"));
		return B_MEDIA_ALREADY_CONNECTED;
	}

	if(format->type != B_MEDIA_RAW_AUDIO) {
		PRINT(("\tBad format type.\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	// do a final validity check:
	status_t err = validateProposedFormat(
		(fFormat.u.raw_audio.format != media_raw_audio_format::wildcard.format) ?
			fFormat :
			fPreferredFormat,
		*format);

	if(err < B_OK) {
		// no go
		return err;
	}

	// fill in wildcards
	specializeOutputFormat(*format);

	// reserve the output
	fOutput.destination = destination;
	fOutput.format = *format;

	// pass back source & output name
	//*source = fOutput.source;
	strncpy(name, fOutput.name, B_MEDIA_NAME_LENGTH);

	return B_OK;
}

status_t
DVDExtractorNode::SetBufferGroup(
	const media_source& source,
	BBufferGroup* group)
{
	PRINT(("DVDExtractorNode::SetBufferGroup()\n"));
	if(source != fOutput.source) {
		PRINT(("\tBad source.\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(fInput.source == media_source::null) {
		PRINT(("\tNo producer to send buffers to.\n"));
		return B_ERROR;
	}

	// +++++ is this right?  buffer-group selection gets
	//       all asynchronous and weird...
	int32 changeTag;
	return SetOutputBuffersFor(
		fInput.source,
		fInput.destination,
		group,
		0, &changeTag);
}

status_t
DVDExtractorNode::SetPlayRate(
	int32 numerator,
	int32 denominator)
{
	// not supported
	return B_ERROR;
}

status_t
DVDExtractorNode::VideoClippingChanged(
	const media_source& source,
	int16 numShorts,
	int16* clipData,
	const media_video_display_info& display,
	int32* fromChangeTag)
{
	// not sane
	return B_ERROR;
}


void
DVDExtractorNode::handleStartEvent(
	const media_timed_event* event)
{
	PRINT(("DVDExtractorNode::handleStartEvent\n"));

	startFilter();
}

void
DVDExtractorNode::handleStopEvent(
	const media_timed_event* event)
{
	PRINT(("DVDExtractorNode::handleStopEvent\n"));

	stopFilter();
}

void
DVDExtractorNode::ignoreEvent(
	const media_timed_event* event)
{
	PRINT(("DVDExtractorNode::ignoreEvent\n"));
}


// -------------------------------------------------------- //
// *** internal operations
// -------------------------------------------------------- //

void
DVDExtractorNode::getPreferredFormat(
	media_format& format)
{
	ASSERT(format.type == B_MEDIA_RAW_AUDIO);

	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.channel_count = 1;
	format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
}


status_t DVDExtractorNode::validateProposedFormat(
	const media_format& preferredFormat,
	media_format& proposedFormat)
{
	char formatStr[256];
	PRINT(("DVDExtractorNode::validateProposedFormat()\n"));

	ASSERT(preferredFormat.type == B_MEDIA_RAW_AUDIO);

	string_for_format(preferredFormat, formatStr, 255);
	PRINT(("\ttemplate format: %s\n", formatStr));

	string_for_format(proposedFormat, formatStr, 255);
	PRINT(("\tproposed format: %s\n", formatStr));

	status_t err = B_OK;

	if(proposedFormat.type != B_MEDIA_RAW_AUDIO) {
		// out of the ballpark
		proposedFormat = preferredFormat;
		return B_MEDIA_BAD_FORMAT;
	}

	// wildcard format
	media_raw_audio_format& wild = media_raw_audio_format::wildcard;
	// proposed format
	media_raw_audio_format& f = proposedFormat.u.raw_audio;
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
		string_for_format(proposedFormat, formatStr, 255);
		PRINT((
			"\tformat conflict; suggesting:\n\tformat %s\n", formatStr));
	}

	return err;
}


void
DVDExtractorNode::specializeOutputFormat(
	media_format& format)
{
	char formatStr[256];
	string_for_format(format, formatStr, 255);
	PRINT(("DVDExtractorNode::specializeOutputFormat()\n"
		"\tinput format: %s\n", formatStr));

	ASSERT(format.type == B_MEDIA_RAW_AUDIO);

	// carpal_tunnel_paranoia
	media_raw_audio_format& f = format.u.raw_audio;
	media_raw_audio_format& w = media_raw_audio_format::wildcard;

	if(f.frame_rate == w.frame_rate)
		f.frame_rate = 44100.0;
	if(f.channel_count == w.channel_count) {
		//+++++ tweaked 15sep99
		if(fInput.source != media_source::null)
			f.channel_count = fInput.format.u.raw_audio.channel_count;
		else
			f.channel_count = 1;
	}
	if(f.format == w.format)
		f.format = media_raw_audio_format::B_AUDIO_FLOAT;
	if(f.byte_order == w.format)
		f.byte_order = (B_HOST_IS_BENDIAN) ? B_MEDIA_BIG_ENDIAN : B_MEDIA_LITTLE_ENDIAN;
	if(f.buffer_size == w.buffer_size)
		f.buffer_size = 2048;

	string_for_format(format, formatStr, 255);
	PRINT(("\toutput format: %s\n", formatStr));
}


// construct delay line if necessary, reset filter state
void
DVDExtractorNode::initFilter()
{
	PRINT(("DVDExtractorNode::initFilter()\n"));
	ASSERT(fFormat.u.raw_audio.format != media_raw_audio_format::wildcard.format);
}


void
DVDExtractorNode::startFilter()
{
	PRINT(("DVDExtractorNode::startFilter()\n"));
}


void
DVDExtractorNode::stopFilter()
{
	PRINT(("DVDExtractorNode::stopFilter()\n"));
}


// figure processing latency by doing 'dry runs' of filterBuffer()
bigtime_t
DVDExtractorNode::calcProcessingLatency()
{
	PRINT(("DVDExtractorNode::calcProcessingLatency()\n"));

	if(fOutput.destination == media_destination::null) {
		PRINT(("\tNot connected.\n"));
		return 0LL;
	}

	// allocate a temporary buffer group
	BBufferGroup* testGroup = new BBufferGroup(
		fOutput.format.u.raw_audio.buffer_size, 1);

	// fetch a buffer
	BBuffer* buffer = testGroup->RequestBuffer(
		fOutput.format.u.raw_audio.buffer_size);
	ASSERT(buffer);

	buffer->Header()->type = B_MEDIA_RAW_AUDIO;
	buffer->Header()->size_used = fOutput.format.u.raw_audio.buffer_size;

	// run the test
	bigtime_t preTest = system_time();
	filterBuffer(buffer);
	bigtime_t elapsed = system_time() - preTest;

	// clean up
	buffer->Recycle();
	delete testGroup;

	// reset filter state
	initFilter();

	return elapsed;
}


void DVDExtractorNode::filterBuffer(
	BBuffer* buffer)
{
}
