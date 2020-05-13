#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "videomode.h"

static int debug = 0;

void DumpSegment(VideoSegmentedScanlineSegment *segment, int currentStart, int indent)
{
    printf("%*s%d: {%f,%f,%f} to {%f,%f,%f} over %d pixels\n", indent, "",
        currentStart,
        segment->r0, segment->g0, segment->b0,
        segment->r1, segment->g1, segment->b1,
        segment->pixelCount);
}

void DumpSegments(VideoSegmentedScanlineSegment *segment, int pixelCount, int indent)
{
    int currentStart = 0;
    while(currentStart < pixelCount) {
        DumpSegment(segment, currentStart, indent);
        currentStart += segment->pixelCount;
        segment++;
    }
}


void SetSegment(VideoSegmentedScanlineSegment *seg, int pixelCount, float r0, float g0, float b0, float r1, float g1, float b1)
{
    seg->pixelCount = pixelCount;
    seg->r0 = r0;
    seg->g0 = g0;
    seg->b0 = b0;
    seg->r1 = r1;
    seg->g1 = g1;
    seg->b1 = b1;
}

static int CalculateSegmentEnd(VideoSegmentedScanlineSegment *seg, int start)
{
    return start + seg->pixelCount - 1;
}

static void ClipSegmentWithNewStart(VideoSegmentedScanlineSegment *src, int oldStart, int newStart, VideoSegmentedScanlineSegment *dst)
{
    if(debug) printf("    ClipSegmentWithNewStart(..., %d, %d, ...)\n", oldStart, newStart);
    dst->pixelCount = src->pixelCount - (newStart - oldStart);
    dst->r0 = src->r0 + (src->r1 - src->r0) / src->pixelCount * (newStart - oldStart);
    dst->g0 = src->g0 + (src->g1 - src->g0) / src->pixelCount * (newStart - oldStart);
    dst->b0 = src->b0 + (src->b1 - src->b0) / src->pixelCount * (newStart - oldStart);
    dst->r1 = src->r1;
    dst->g1 = src->g1;
    dst->b1 = src->b1;
}

static void ClipSegmentWithNewEnd(VideoSegmentedScanlineSegment *src, int newCount, VideoSegmentedScanlineSegment *dst)
{
    dst->pixelCount = newCount;
    dst->r0 = src->r0;
    dst->g0 = src->g0;
    dst->b0 = src->b0;
    dst->r1 = src->r0 + (src->r1 - src->r0) / src->pixelCount * newCount;
    dst->g1 = src->g0 + (src->g1 - src->g0) / src->pixelCount * newCount;
    dst->b1 = src->b0 + (src->b1 - src->b0) / src->pixelCount * newCount;
}

int MergeSegment(VideoSegmentedScanlineSegment *newSegment, int start, VideoSegmentedScanlineSegment *oldSegments, int pixelCount, VideoSegmentedScanlineSegment *resultSegments, int maxNewSegmentCount, int *newCount)
{
    VideoSegmentedScanlineSegment *src = oldSegments;
    VideoSegmentedScanlineSegment *dst = resultSegments;
    int srcSegStart = 0;
    int segmentCount = 0;

    // If new segment is zero length, it wouldn't insert anything, so return success;
    if(newSegment->pixelCount == 0) {
        return 0;
    }

    // Copy all segments entirely before this new segment
    while(CalculateSegmentEnd(src, srcSegStart) < start) {
        if(segmentCount >= maxNewSegmentCount) {
            return 1; // out of space copying start segments
        }
        if(debug) printf("    copy old segment before\n");
        *dst = *src;
        srcSegStart += src->pixelCount; segmentCount++; dst++; src++;
    }

    // Copy the segment that is overlapped by the start of the new segment, if there is one
    if(srcSegStart < start) {
        if(segmentCount >= maxNewSegmentCount) {
            return 2; // out of space clipping segment overlapped at beginning
        }
        if(debug) printf("    copy old segment overlapping beginning\n");
        ClipSegmentWithNewEnd(src, start - srcSegStart, dst);
        if(debug) {
            printf("    new segment = ");
            DumpSegment(dst, srcSegStart, 0);
        }
        segmentCount++; dst++;
    }

    // Put in the new segment
    if(segmentCount >= maxNewSegmentCount) {
        return 3; // out of space copying new segment
    }
    if(debug) printf("    copy in new segment\n");

    dst->r0 = newSegment->r0;
    dst->g0 = newSegment->g0;
    dst->b0 = newSegment->b0;
    dst->r1 = newSegment->r1;
    dst->g1 = newSegment->g1;
    dst->b1 = newSegment->b1;
    dst->pixelCount = newSegment->pixelCount;
    segmentCount++;
    dst++;

    if(debug) {
        printf("    next segment entering skip loop body = ");
        DumpSegment(src, srcSegStart, 0);
    }

    // Skip until segments not covered by the new segment
    while( (srcSegStart <= CalculateSegmentEnd(newSegment, start)) &&
        (CalculateSegmentEnd(src, srcSegStart) <= CalculateSegmentEnd(newSegment, start))) {

        if(debug) printf("    skip old overlapped segment\n");
        srcSegStart += src->pixelCount;
        src++;
        if(debug) {
            if( (srcSegStart <= CalculateSegmentEnd(newSegment, start)) &&
                (CalculateSegmentEnd(src, srcSegStart) <= CalculateSegmentEnd(newSegment, start))) {

                printf("    possible next segment end of skip loop body = ");
                DumpSegment(src, srcSegStart, 0);
            }
        }
    }

    // Copy the segment that is overlapped by the end of the new segment, if there is one
    if(srcSegStart <= CalculateSegmentEnd(newSegment, start)) {
        if(segmentCount >= maxNewSegmentCount) {
            return 4; // out of space clipping segment overlapped at end
        }
        if(debug) printf("    copy segment overlapped at end\n");
        ClipSegmentWithNewStart(src, srcSegStart, CalculateSegmentEnd(newSegment, start) + 1, dst);
        if(debug) {
            printf("    new segment = ");
            DumpSegment(dst, srcSegStart, 0);
        }
        srcSegStart += src->pixelCount; segmentCount++; src++; dst++;
    }

    // Copy all segments entirely after this new segment
    while(srcSegStart < pixelCount) {
        if(segmentCount >= maxNewSegmentCount) {
            return 5; // out of space copying end segments
        }
        if(debug) printf("    copy segment after end of new segment\n");
        *dst = *src;
        srcSegStart += src->pixelCount; segmentCount++; dst++; src++;
    }

    *newCount = segmentCount; // For validation purposes, not used by video mode
    return 0;
}

typedef struct VideoSegmentBuffer {
    int scanlineWidth;  // pixels in scanline
    int segmentsInPool; // Total number of segments allocated
    VideoSegmentedScanlineSegment *segmentPool;
    int scanlineCount;  // Total number of scanlines allocated
    VideoSegmentedScanline *scanlines;
    int rowBeingUpdated;
    VideoSegmentedScanlineSegment *currentSegmentDestination;
    VideoSegmentedScanlineSegment *segmentCeiling;
} VideoSegmentBuffer;

int VideoBufferAllocateMembers(VideoSegmentBuffer *buffer, int width, int totalSegments, int scanlineCount, float r, float g, float b)
{
    // segment parameters
    buffer->segmentsInPool = totalSegments;
    buffer->segmentPool = (VideoSegmentedScanlineSegment*)malloc(sizeof(VideoSegmentedScanlineSegment) * totalSegments);
    if(buffer->segmentPool == NULL) {
        return 1; // couldn't allocate segment pool 
    }

    // Scanline parameters
    buffer->scanlineWidth = width;
    buffer->scanlineCount = scanlineCount;
    buffer->scanlines = (VideoSegmentedScanline*)malloc(sizeof(VideoSegmentedScanline) * scanlineCount);
    if(buffer->scanlines == NULL) {
        free(buffer->segmentPool);
        return 2; // couldn't allocate segment pool 
    }

    // Row update parameters
    buffer->rowBeingUpdated = -1;
    buffer->currentSegmentDestination = NULL;

    // Set scanlines to a default empty segment
    for(int i = 0; i < scanlineCount; i++) {
        buffer->scanlines[i].segmentCount = 1;
        buffer->scanlines[i].segments = buffer->segmentPool + i;
        buffer->scanlines[i].segments[0].pixelCount = width;
        buffer->scanlines[i].segments[0].r0 = r;
        buffer->scanlines[i].segments[0].g0 = g;
        buffer->scanlines[i].segments[0].b0 = b;
        buffer->scanlines[i].segments[0].r1 = r;
        buffer->scanlines[i].segments[0].g1 = g;
        buffer->scanlines[i].segments[0].b1 = b;
    }

    return 0;
}

void VideoBufferFreeMembers(VideoSegmentBuffer *buffer)
{
    free(buffer->segmentPool);
    free(buffer->scanlines);
}

int VideoBufferBeginUpdate(VideoSegmentBuffer *buffer)
{
    if(buffer->rowBeingUpdated != -1) {
        return 1; // Update is already in progress
    }
    buffer->rowBeingUpdated = 0;

    VideoSegmentedScanlineSegment *segp = buffer->segmentPool + buffer->segmentsInPool;

    // Move all rows to top of buffer
    for(int i = buffer->scanlineCount; i >= 0; i--) {
        segp -= buffer->scanlines[i].segmentCount;
        memcpy(segp, buffer->scanlines[i].segments, sizeof(VideoSegmentedScanlineSegment) * buffer->scanlines[i].segmentCount);
        buffer->scanlines[i].segments = segp;
    }

    // Set new update destination as beginning of buffer
    buffer->currentSegmentDestination = buffer->segmentPool;

    // Set ceiling (cannot go into or past) to bottom of copied segments
    buffer->segmentCeiling = segp;

    return 0;
}

int VideoBufferGetCurrentRowForUpdate(VideoSegmentBuffer *buffer, VideoSegmentedScanlineSegment** curSegments, int *segmentCount)
{
    if(buffer->rowBeingUpdated == -1) {
        return 1; // Not updating
    }

    *curSegments = buffer->scanlines[buffer->rowBeingUpdated].segments;
    *segmentCount = buffer->scanlines[buffer->rowBeingUpdated].segmentCount;

    return 0;
}

// Updates current row, increments current row
int VideoBufferUpdateRow(VideoSegmentBuffer *buffer, VideoSegmentedScanlineSegment* newSegments, int newSegmentCount)
{
    int row = buffer->rowBeingUpdated;

    if(row == -1) {
        return 1; // Not updating
    }

    if(row >= buffer->scanlineCount) {
        return 2; // Updated too many rows
    }

    // Delete old scanline's segments
    buffer->segmentCeiling += buffer->scanlines[row].segmentCount;

    if(buffer->segmentCeiling - buffer->currentSegmentDestination < newSegmentCount) {
        return 3; // Not enough room for new scanline
    }

    // copy segments in, set scanline location
    memcpy(buffer->currentSegmentDestination, newSegments, sizeof(VideoSegmentedScanlineSegment) * newSegmentCount);
    buffer->scanlines[row].segmentCount = newSegmentCount;
    buffer->scanlines[row].segments = buffer->currentSegmentDestination;
    buffer->currentSegmentDestination += newSegmentCount;

    buffer->rowBeingUpdated++;

    if(buffer->rowBeingUpdated == buffer->scanlineCount) {
        // Finished updating rows, stop
        buffer->rowBeingUpdated = -1;
    }

    return 0;
}
