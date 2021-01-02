#ifndef _ROCINANTE_H_
#define _ROCINANTE_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define ROCINANTE 1

// XXX constexpr size_t MAX_RAM = 300 * 1024;  // XXX unless I upgrade to static RAM or other part

typedef enum Status {
    RO_SUCCESS = 0,
    NO_VIDEO_SUBSYSTEM_SET = -1,        // The platform did not set the video subsystem
    INVALID_VIDEO_MODE_NUMBER = -2,     // The index passed was not in the range of valid modes
    INVALID_STRUCTURE_SIZE = -3,        // The "size" parameter did not match the size of the requested structure
    INVALID_STRUCTURE_TYPE = -4,        // The "type" parameter did not match the size of the requested structure
    VIDEO_MODE_DOES_NOT_MATCH = -5,     // The "type" parameter to VideoModeGetInfo did not match the requested mode
    VIDEO_MODE_INFO_UNSUPPORTED = -6,   // The video subsystem does not support returning info on the requested mode
    INVALID_WINDOW = -7,                // A window was not open or valid with the provided index
    INVALID_PARAMETER_VALUE = -8,       // A passed parameter was outside the valid range
    WINDOW_CREATION_FAILED = -9,        // Window could not be created because of memory allocation or possibly other reason
    RESOURCE_EXHAUSTED = -10,           // There were no more objects of the requested type
    SIZE_EXCEEDED = -11,                // Window could not be created because of memory allocation or possibly other reason
} Status;

/*! Set colors on the 3 RGB LEDs.
    \param which Index of the LED to set; 0, 1, or 2.
    \param red Red component, 0 to 255.
    \param green Green component, 0 to 255.
    \param blue Blue component, 0 to 255.
    \return RO_SUCCESS, or INVALID_PARAMETER_VALUE for LED > 2
*/
int RoLEDSet(int which, uint8_t red, uint8_t green, uint8_t blue);

/*! Add a line to the debug overlay.  The actual formatting function is vsnprintf.
    \param fmt The printf-style format.
    \param ... Arguments used in the format.
*/
void RoDebugOverlayPrintf(const char *fmt, ...);

/*! Set a line in the debug overlay.  The actual formatting function is vsnprintf.
    \param line The line to set.
    \param str The buffer to copy.
    \param size The number of bytes to copy.
*/
void RoDebugOverlaySetLine(int line, const char *str, size_t size);

/*! Get stereo audio stream info.
    \param rate Populated with the sampling rate (samples per second).
    \param bufferLength Populated with the number of 2-byte (one byte each left and right) unsigned (0-255) samples in the buffer.
    \param stereoBufferU8 Populated with the location of the buffer.
*/
void RoAudioGetSamplingInfo(float *rate, size_t *bufferLength, uint8_t **stereoBufferU8);

/*! Block until a half-buffer even boundary in the audio stream.  This function is useful for blocking until the system has just finished streaming samples within the half of the audio buffer starting at the return value.
    \return Where application should now fill one half the buffer's worth of audio.
*/
size_t RoAudioBlockToHalfBuffer();

/*! Write audio samples.  This function will copy a mono U8 buffer with half the samples reported by RoAudioGetSamplingInfo into the audio stream.
    \param where Where to write samples (returned from RoAudioBlockToHalfBuffer).
    \param monoBufferU8 The buffer of mono unsigned samples to write into the audio stream buffer.
    \return INVALID_PARAMETER_VALUE if where was outside of the audio buffer
*/
Status RoAudioSetHalfBufferMonoSamples(size_t where, const uint8_t *monoBufferU8);

/*! Clear the audio stream to silence
*/
void RoAudioClear();

void RoProcessYield(void);

void panic(void);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* _ROCINANTE_H_ */
