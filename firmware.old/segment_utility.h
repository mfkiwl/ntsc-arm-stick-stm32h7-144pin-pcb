#ifndef _SEGMENT_UTILITY_H_
#define _SEGMENT_UTILITY_H_

#include "videomode.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void DumpSegment(VideoSegmentedScanlineSegment *segment, int currentStart, int indent);

void DumpSegments(VideoSegmentedScanlineSegment *segment, int pixelCount, int indent);

void SegmentSetGradient(VideoSegmentedScanlineSegment *seg, int pixelCount, float r0, float g0, float b0, float r1, float g1, float b2);

int MergeSegment(VideoSegmentedScanlineSegment *newSegment, int start, VideoSegmentedScanlineSegment *oldSegments, int pixelCount, VideoSegmentedScanlineSegment *resultSegments, int maxNewSegmentCount, int *newCount);

typedef struct VideoSegmentBuffer
{
    int scanlineWidth;  // pixels in scanline
    int segmentsInPool; // Total number of segments allocated
    VideoSegmentedScanlineSegment *segmentPool;
    int scanlineCount;  // Total number of scanlines allocated
    VideoSegmentedScanline *scanlines;
    int rowBeingUpdated;
    VideoSegmentedScanlineSegment *currentSegmentDestination;
    VideoSegmentedScanlineSegment *segmentCeiling;
} VideoSegmentBuffer;

// Allocate all member arrays or return != 0
int VideoBufferAllocateMembers(VideoSegmentBuffer *buffer, int width, int totalSegments, int scanlineCount, float r, float g, float b);

// Free all member arrays
void VideoBufferFreeMembers(VideoSegmentBuffer *buffer);

// Reset all scanlines to a single segment with the solid supplied color
int VideoBufferReset(VideoSegmentBuffer *buffer, float r, float g, float b);

// Begin buffer updates; must update all scanline rows in increasing
// order by calling VideoBufferGetCurrentRowForUpdate and then
// VideoBufferFinishCurrentRowUpdate.
int VideoBufferBeginUpdate(VideoSegmentBuffer *buffer);

// Get current row's current segments and target for new
// segments (availableSegments). EITHER copy all the old segments
// to the new target or copy a new version of the old segments (e.g.
// MergeSegment has been executed)
int VideoBufferGetCurrentRowForUpdate(VideoSegmentBuffer *buffer, VideoSegmentedScanlineSegment** curSegments, int *segmentCount, VideoSegmentedScanlineSegment** availableSegments, int *availableCount);

// Update current row's pointer and count to the previously returned
// availableSegments pointer and provided count.
int VideoBufferFinishCurrentRowUpdate(VideoSegmentBuffer *buffer, int newSegmentCount);

int CircleToSegmentsSolid(VideoSegmentBuffer *buffer, int cx, int cy, int cr, float r, float g, float b);

typedef struct GradientDescriptor
{
    float r0, g0, b0;
    float dr, dg, db;
    float m[3][3];
} GradientDescriptor;

void GradientSet(GradientDescriptor *grad, float x0, float y0, float r0, float g0, float b0, float x1, float y1, float r1, float g1, float b1);

int CircleToSegmentsGradient(VideoSegmentBuffer *buffer, int cx, int cy, int cr, GradientDescriptor *grad);

int IntersectSegmentAtY(float x0, float y0, float x1, float y1, float y, float *x);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* _SEGMENT_UTILITY_H_ */
