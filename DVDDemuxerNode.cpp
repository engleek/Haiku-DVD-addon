#include "DVDDemuxerNode.h"

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
                printf("DVDDemuxerNode::"); \
                printf b; \
            } \
        } while (0)

DVDDemuxerNode::~DVDDemuxerNode()
{
	// shut down
	Quit();
}


DVDDemuxerNode::DVDDemuxerNode(const flavor_info *info, BMediaAddOn* addOn) :
	// base classes
	BMediaNode(info->name),
	BBufferConsumer(B_MEDIA_MULTISTREAM),
	BBufferProducer(B_MEDIA_UNKNOWN_TYPE),
	BMediaEventLooper(),

	// flavor
	fFlavorInfo(*info),

	// connection state
	fOutputsEnabled(true),
	fDownstreamLatency(0),
	fProcessingLatency(0),

	// add-on
	fAddOn(addOn)
{}


// -------------------------------------------------------- //
// *** BMediaNode
// -------------------------------------------------------- //

status_t
DVDDemuxerNode::HandleMessage(
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
DVDDemuxerNode::AddOn(
	int32* ID) const
{

	if(fAddOn)
		*ID = 0;
	return fAddOn;
}


status_t
DVDDemuxerNode::InitCheck(void) const
{
	fprintf(stderr,"DVDDemuxerNode::InitCheck\n");
	return fInitCheckStatus;
}


// -------------------------------------------------------- //
// *** BMediaEventLooper
// -------------------------------------------------------- //

void
DVDDemuxerNode::HandleEvent(
	const media_timed_event* event,
	bigtime_t howLate,
	bool realTimeEvent)
{
	switch(event->type) {
		case BTimedEventQueue::B_START:
			handleStart(event);
			break;

		case BTimedEventQueue::B_STOP:
			handleStop(event);
			break;

    case BTimedEventQueue::B_DATA_STATUS:
    case BTimedEventQueue::B_PARAMETER:
    default:
      PRINTF(-1, ("HandleEvent: Unhandled event -- %lx\n", event->type));
      break;
	}
}


void
DVDDemuxerNode::NodeRegistered()
{
	PRINT(("DVDDemuxerNode::NodeRegistered()\n"));

	// Input data
	fInput.node = Node();
	fInput.destination.id = 0;
	fInput.destination.port = ControlPort();
    fInput.format.type = B_MEDIA_MULTISTREAM;
    fInput.format.u.multistream = media_multistream_format::wildcard;
    fInput.format.u.multistream.format = media_multistream_format::B_MPEG2;
	
	// Video output data
	fOutputs[0].node = Node();
	fOutputs[0].source.id = 0;
	fOutputs[0].source.port = ControlPort();
	fOutputs[0].format.type = B_MEDIA_ENCODED_VIDEO;
	fOutputs[0].format.u.encoded_video = media_encoded_video_format::wildcard;
	
	// Video output data
	fOutputs[1].node = Node();
	fOutputs[1].source.id = 1;
	fOutputs[1].source.port = ControlPort();
	fOutputs[1].format.type = B_MEDIA_ENCODED_AUDIO;
	fOutputs[1].format.u.encoded_audio = media_encoded_audio_format::wildcard;

	// Video output data
	fOutputs[2].node = Node();
	fOutputs[2].source.id = 3;
	fOutputs[2].source.port = ControlPort();
	fOutputs[2].format.type = B_MEDIA_FIRST_USER_TYPE;

	// Start the BMediaEventLooper thread
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
}


bigtime_t DVDDemuxerNode::OfflineTime()
{
	fprintf(stderr,"DVDDemuxerNode(BMediaEventLooper)::OfflineTime\n");
	return BMediaEventLooper::OfflineTime();
}


// -------------------------------------------------------- //
// *** BBufferConsumer
// -------------------------------------------------------- //

status_t
DVDDemuxerNode::AcceptFormat(
	const media_destination& destination,
	media_format* format)
{

	PRINT(("DVDDemuxerNode::AcceptFormat()\n"));

	// sanity checks
	if(destination != fInput.destination) {
		PRINT(("\tbad destination\n"));
		return B_MEDIA_BAD_DESTINATION;
	}
	if(format->type != B_MEDIA_MULTISTREAM) {
		PRINT(("\tnot B_MEDIA_MULTISTREAM\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	validateProposedFormat(
		(fFormat.u.multistream.format != media_multistream_format::wildcard.format) ?
			fFormat : fPreferredFormat,
		*format);
		
	return B_OK;
}


void
DVDDemuxerNode::BufferReceived(
	BBuffer* buffer)
{
	// check buffer destination
	if(buffer->Header()->destination !=
		fInput.destination.id) {
		PRINT(("DVDDemuxerNode::BufferReceived():\n"
			"\tBad destination.\n"));
		buffer->Recycle();
		return;
	}

	if(buffer->Header()->time_source != TimeSource()->ID()) {
		PRINT(("* timesource mismatch\n"));
	}

	// check outputs
	if(fOutputs[0].destination == media_destination::null
	|| fOutputs[1].destination == media_destination::null
	|| fOutputs[2].destination == media_destination::null
	|| !fOutputsEnabled) {
		buffer->Recycle();
		return;
	}

	/***********************
	 * Extractor action here
	 * 1 buffer goes in
	 * 3 buffers come out
	 ***********************/

	status_t err = SendBuffer(buffer, fOutputs[0].source, fOutputs[0].destination);
	if (err < B_OK) {
		PRINT(("DVDDemuxerNode::BufferReceived():\n"
			"\tSendBuffer() failed: %s\n", strerror(err)));
		buffer->Recycle();
	}
}


status_t
DVDDemuxerNode::Connected(const media_source& source,
	const media_destination& destination, const media_format& format,
	media_input* input)
{
	PRINT(("DVDDemuxerNode::Connected()\n"
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
DVDDemuxerNode::Disconnected(
	const media_source& source,
	const media_destination& destination)
{

	PRINT(("DVDDemuxerNode::Disconnected()\n"));

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
	if(fOutputs[0].destination == media_destination::null
	|| fOutputs[1].destination == media_destination::null
	|| fOutputs[2].destination == media_destination::null) {
		fFormat.u.multistream = media_multistream_format::wildcard;
	}

	fInput.format = fFormat;
}


void
DVDDemuxerNode::DisposeInputCookie(
	int32 cookie)
{}


status_t
DVDDemuxerNode::FormatChanged(
	const media_source& source,
	const media_destination& destination,
	int32 changeTag,
	const media_format& newFormat)
{
	// flat-out deny format changes
	return B_MEDIA_BAD_FORMAT;
}


status_t
DVDDemuxerNode::GetLatencyFor(
	const media_destination& destination,
	bigtime_t* latency,
	media_node_id* timeSource)
{
	PRINT(("DVDDemuxerNode::GetLatencyFor()\n"));

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
DVDDemuxerNode::GetNextInput(
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
DVDDemuxerNode::ProducerDataStatus(
	const media_destination& destination,
	int32 status,
	bigtime_t when)
{
	PRINT(("DVDDemuxerNode::ProducerDataStatus()\n"));

	// sanity check
	if(destination != fInput.destination) {
		PRINT(("\tbad destination\n"));
	}

	if(fOutputs[0].destination != media_destination::null) {
		// pass status downstream
		status_t err = SendDataStatus(
			status,
			fOutputs[0].destination,
			when);
		if(err < B_OK) {
			PRINT(("\tSendDataStatus(): %s\n", strerror(err)));
		}
	}

	if(fOutputs[1].destination != media_destination::null) {
		// pass status downstream
		status_t err = SendDataStatus(
			status,
			fOutputs[1].destination,
			when);
		if(err < B_OK) {
			PRINT(("\tSendDataStatus(): %s\n", strerror(err)));
		}
	}

	if(fOutputs[2].destination != media_destination::null) {
		// pass status downstream
		status_t err = SendDataStatus(
			status,
			fOutputs[2].destination,
			when);
		if(err < B_OK) {
			PRINT(("\tSendDataStatus(): %s\n", strerror(err)));
		}
	}
}


status_t
DVDDemuxerNode::SeekTagRequested(
	const media_destination& destination,
	bigtime_t targetTime,
	uint32 in_flags,
	media_seek_tag* seekTag,
	bigtime_t* taggedTime,
	uint32* out_flags)
{
	PRINT(("DVDDemuxerNode::SeekTagRequested()\n"
		"\tNot implemented.\n"));

	return B_ERROR;
}


// -------------------------------------------------------- //
// *** BBufferProducer
// -------------------------------------------------------- //

void
DVDDemuxerNode::AdditionalBufferRequested(
	const media_source& source,
	media_buffer_id previousBufferID,
	bigtime_t previousTime,
	const media_seek_tag* previousTag)
{

	PRINT(("DVDDemuxerNode::AdditionalBufferRequested\n"
		"\tOffline mode not implemented."));
}

void
DVDDemuxerNode::Connect(
	status_t status,
	const media_source& source,
	const media_destination& destination,
	const media_format& format,
	char* name)
{

	PRINT(("DVDDemuxerNode::Connect()\n"));
	//status_t err;

	if (fOutputs[0].source == source) {
		fOutputs[0].format = format;
		fOutputs[0].destination = destination;
		strcpy(name, fOutputs[0].name);
	} else if (fOutputs[1].source == source) {
		fOutputs[1].format = format;
		fOutputs[1].destination = destination;
		strcpy(name, fOutputs[1].name);
	} else if (fOutputs[2].source == source) {
		fOutputs[2].format = format;
		fOutputs[2].destination = destination;
		strcpy(name, fOutputs[2].name);
	} else {
		// we don't have that output
		fprintf(stderr,"<- B_MEDIA_BAD_SOURCE\n");
		return;
	}

	// connection failed?
	if(status < B_OK) {
		PRINT(("\tStatus: %s\n", strerror(status)));
		return;
	}

	// figure processing time
	fProcessingLatency = calcProcessingLatency();
	PRINT(("\tprocessing latency = %Ld\n", fProcessingLatency));

	// store summed latency
	SetEventLatency(fDownstreamLatency + fProcessingLatency);

// Sending latency upstream to the DVDDisk node isn't really necessary
/*
	if(fInput.source != media_source::null) {
		// pass new latency upstream
		err = SendLatencyChange(
			fInput.source,
			fInput.destination,
			EventLatency() + SchedulingLatency());
		if(err < B_OK)
			PRINT(("\t!!! SendLatencyChange(): %s\n", strerror(err)));
	}
*/
}

void
DVDDemuxerNode::Disconnect(
	const media_source& source,
	const media_destination& destination)
{
	PRINT(("DVDDemuxerNode::Disconnect()\n"));

	// sanity checks
	if (source == fOutputs[0].source) {
		if(destination != fOutputs[0].destination) {
			PRINT(("\tbad destination\n"));
			return;
		}
		
		// clean up
		fOutputs[0].destination = media_destination::null;
		fOutputs[0].format = fFormat;
	} else if (source == fOutputs[1].source) {
		if(destination != fOutputs[1].destination) {
			PRINT(("\tbad destination\n"));
			return;
		}
		
		// clean up
		fOutputs[1].destination = media_destination::null;
		fOutputs[1].format = fFormat;
	} else if (source == fOutputs[2].source) {
		if(destination != fOutputs[2].destination) {
			PRINT(("\tbad destination\n"));
			return;
		}

		// clean up
		fOutputs[2].destination = media_destination::null;
		fOutputs[2].format = fFormat;
	} else {
		PRINT(("\tbad source\n"));
		return;
	}

	// no input? clear format:
	if(fInput.source == media_source::null) {
		fFormat.u.multistream = media_multistream_format::wildcard;
	}
}

status_t
DVDDemuxerNode::DisposeOutputCookie(
	int32 cookie)
{
	return B_OK;
}

void
DVDDemuxerNode::EnableOutput(
	const media_source& source,
	bool enabled,
	int32* _deprecated_)
{
	PRINT(("DVDDemuxerNode::EnableOutput()\n"));
	if(source != fOutputs[0].source
	   || source != fOutputs[1].source
	   || source != fOutputs[2].source) {
		PRINT(("\tbad source\n"));
		return;
	}

	fOutputsEnabled = enabled;
}


status_t
DVDDemuxerNode::FormatChangeRequested(
	const media_source& source,
	const media_destination& destination,
	media_format* format,
	int32* _deprecated_)
{

	// deny
	PRINT(("DVDDemuxerNode::FormatChangeRequested()\n"
		"\tNot supported.\n"));

	return B_MEDIA_BAD_FORMAT;
}

status_t
DVDDemuxerNode::FormatProposal(
	const media_source& source,
	media_format* format)
{
	PRINT(("DVDDemuxerNode::FormatProposal()\n"));

	if(source != fOutputs[0].source
	&& source != fOutputs[1].source
	&& source != fOutputs[2].source) {
		PRINT(("\tbad source\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(format->type != B_MEDIA_MULTISTREAM) {
		PRINT(("\tbad type\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	validateProposedFormat(
		(fFormat.u.multistream.format != media_multistream_format::wildcard.format) ?
			fFormat :
			fPreferredFormat,
		*format);
	return B_OK;
}


status_t
DVDDemuxerNode::FormatSuggestionRequested(
	media_type type,
	int32 quality,
	media_format* format)
{
	PRINT(("DVDDemuxerNode::FormatSuggestionRequested()\n"));

	if(type != B_MEDIA_MULTISTREAM) {
		PRINT(("\tbad type\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	if(fFormat.u.multistream.format != media_multistream_format::wildcard.format)
		*format = fFormat;
	else
		*format = fPreferredFormat;
	return B_OK;
}

status_t
DVDDemuxerNode::GetLatency(
	bigtime_t* latency)
{
	PRINT(("DVDDemuxerNode::GetLatency()\n"));
	*latency = EventLatency() + SchedulingLatency();
	PRINT(("\treturning %Ld\n", *latency));

	return B_OK;
}

status_t
DVDDemuxerNode::GetNextOutput(
	int32* cookie,
	media_output* output)
{
	if(*cookie < 0 || *cookie > 2)
		return B_BAD_INDEX;

	*output = fOutputs[*cookie];
	++*cookie;

	return B_OK;
}


void
DVDDemuxerNode::LatencyChanged(
	const media_source& source,
	const media_destination& destination,
	bigtime_t newLatency,
	uint32 flags)
{
	PRINT(("DVDDemuxerNode::LatencyChanged()\n"));

	if(source != fOutputs[0].source
	&& source != fOutputs[1].source
	&& source != fOutputs[2].source) {
		PRINT(("\tBad source\n"));
		return;
	}

	if(destination != fOutputs[0].destination
	&& destination != fOutputs[1].destination
	&& destination != fOutputs[2].destination) {
		PRINT(("\tBad destination\n"));
		return;
	}

	fDownstreamLatency = newLatency;
	SetEventLatency(fDownstreamLatency + fProcessingLatency);

// Again, not sure this is necessary
/*
	if(fInput.source != media_source::null) {
		// pass new latency upstream
		status_t err = SendLatencyChange(
			fInput.source,
			fInput.destination,
			EventLatency() + SchedulingLatency());
		if(err < B_OK)
			PRINT(("\t!!! SendLatencyChange(): %s\n", strerror(err)));
	}
*/
}

void
DVDDemuxerNode::LateNoticeReceived(
	const media_source& source,
	bigtime_t howLate,
	bigtime_t when)
{
	PRINT(("DVDDemuxerNode::LateNoticeReceived()\n"
		"\thowLate == %Ld\n"
		"\twhen    == %Ld\n", howLate, when));

	if(source != fOutputs[0].source
	&& source != fOutputs[1].source
	&& source != fOutputs[2].source) {
		PRINT(("\tBad source\n"));
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
DVDDemuxerNode::PrepareToConnect(
	const media_source& source,
	const media_destination& destination,
	media_format* format,
	media_source* out_source,
	char* name)
{
	char formatStr[256];
	string_for_format(*format, formatStr, 255);
	PRINT(("DVDDemuxerNode::PrepareToConnect()\n"
		"\tproposed format: %s\n", formatStr));

	if(source != fOutputs[0].source
	&& source != fOutputs[1].source
	&& source != fOutputs[2].source) {
		PRINT(("\tBad source\n"));
		return B_MEDIA_BAD_SOURCE;
	}

	if(fOutputs[0].destination != media_destination::null
	&& fOutputs[1].destination != media_destination::null
	&& fOutputs[2].destination != media_destination::null) {
		PRINT(("\tAlready connected.\n"));
		return B_MEDIA_ALREADY_CONNECTED;
	}

	if(format->type != B_MEDIA_MULTISTREAM) {
		PRINT(("\tBad format type.\n"));
		return B_MEDIA_BAD_FORMAT;
	}

	// do a final validity check:
	status_t err = validateProposedFormat(
		(fFormat.u.multistream.format != media_multistream_format::wildcard.format) ?
			fFormat :
			fPreferredFormat,
		*format);

	if(err < B_OK) {
		// no go
		return err;
	}

	// fill in wildcards
	specializeOutputFormat(*format);

	if (source == fOutputs[0].source) {
		// reserve the output
		fOutputs[0].destination = destination;
		fOutputs[0].format = *format;

		// pass back source & output name
		*out_source = fOutputs[0].source;
		strncpy(name, fOutputs[0].name, B_MEDIA_NAME_LENGTH);
	} else if (source == fOutputs[1].source) {
		// reserve the output
		fOutputs[1].destination = destination;
		fOutputs[1].format = *format;

		// pass back source & output name
		*out_source = fOutputs[1].source;
		strncpy(name, fOutputs[1].name, B_MEDIA_NAME_LENGTH);
	} else if (source == fOutputs[2].source) {
		// reserve the output
		fOutputs[2].destination = destination;
		fOutputs[2].format = *format;

		// pass back source & output name
		*out_source = fOutputs[2].source;
		strncpy(name, fOutputs[2].name, B_MEDIA_NAME_LENGTH);
	}
	
	return B_OK;
}

status_t
DVDDemuxerNode::SetBufferGroup(
	const media_source& source,
	BBufferGroup* group)
{
	PRINT(("DVDDemuxerNode::SetBufferGroup()\n"));
	if(source != fOutputs[0].source
	&& source != fOutputs[1].source
	&& source != fOutputs[2].source) {
		PRINT(("\tBad source\n"));
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
DVDDemuxerNode::SetPlayRate(
	int32 numerator,
	int32 denominator)
{
	// not supported
	return B_ERROR;
}

status_t
DVDDemuxerNode::VideoClippingChanged(
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
DVDDemuxerNode::handleStart(
	const media_timed_event* event)
{
	PRINT(("DVDDemuxerNode::handleStartEvent\n"));

	start();
}

void
DVDDemuxerNode::handleStop(
	const media_timed_event* event)
{
	PRINT(("DVDDemuxerNode::handleStopEvent\n"));

	stop();
}

void
DVDDemuxerNode::ignoreEvent(
	const media_timed_event* event)
{
	PRINT(("DVDDemuxerNode::ignoreEvent\n"));
}


// -------------------------------------------------------- //
// *** internal operations
// -------------------------------------------------------- //

void
DVDDemuxerNode::getPreferredFormat(
	media_format& format)
{
	format.u.multistream = media_multistream_format::wildcard;
    format.u.multistream.format = media_multistream_format::B_MPEG2;
}


status_t DVDDemuxerNode::validateProposedFormat(
	const media_format& preferredFormat,
	media_format& proposedFormat)
{
	char formatStr[256];
	PRINT(("DVDDemuxerNode::validateProposedFormat()\n"));

	string_for_format(preferredFormat, formatStr, 255);
	PRINT(("\ttemplate format: %s\n", formatStr));

	string_for_format(proposedFormat, formatStr, 255);
	PRINT(("\tproposed format: %s\n", formatStr));

	status_t err = B_OK;

	if(proposedFormat.type != B_MEDIA_MULTISTREAM) {
		// out of the ballpark
		proposedFormat = preferredFormat;
		return B_MEDIA_BAD_FORMAT;
	}

	// wildcard format
	media_multistream_format& wild = media_multistream_format::wildcard;
	// proposed format
	media_multistream_format& f = proposedFormat.u.multistream;
	// template format
	const media_multistream_format& pref = preferredFormat.u.multistream;

	if(pref.format != wild.format) {
		if(f.format != pref.format) {
			if(f.format != wild.format)
				err = B_MEDIA_BAD_FORMAT;
			f.format = pref.format;
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
DVDDemuxerNode::specializeOutputFormat(
	media_format& format)
{
	char formatStr[256];
	string_for_format(format, formatStr, 255);
	PRINT(("DVDDemuxerNode::specializeOutputFormat()\n"
		"\tinput format: %s\n", formatStr));

	// carpal_tunnel_paranoia
	media_multistream_format& f = format.u.multistream;
	media_multistream_format& w = media_multistream_format::wildcard;

	if(f.format == w.format)
		f.format = media_multistream_format::B_MPEG2;

	string_for_format(format, formatStr, 255);
	PRINT(("\toutput format: %s\n", formatStr));
}


void
DVDDemuxerNode::start()
{
	PRINT(("DVDDemuxerNode::start()\n"));
}


void
DVDDemuxerNode::stop()
{
	PRINT(("DVDDemuxerNode::stop()\n"));
}


// figure processing latency by doing 'dry runs' of buffer()
bigtime_t
DVDDemuxerNode::calcProcessingLatency()
{
	PRINT(("DVDDemuxerNode::calcProcessingLatency()\n"));

	/**********************
	 * Complicated business
	 * Return a big number for now
	 * TODO!
	 **********************/

	return 500;

}


void DVDDemuxerNode::buffer(
	BBuffer* buffer)
{
}
