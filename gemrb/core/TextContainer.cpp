/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2014 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "TextContainer.h"

#include "Font.h"
#include "Interface.h"
#include "Palette.h"
#include "Sprite2D.h"
#include "System/String.h"
#include "Video.h"

namespace GemRB {

TextSpan::TextSpan(const String& string, Font* fnt, Palette* pal)
	: text(string)
{
	font = fnt;
	spanSprite = NULL;
	frame = font->StringSize(text);

	pal->acquire();
	palette = pal;
}

TextSpan::TextSpan(const String& string, Font* fnt, Palette* pal, const Size& frame)
	: text(string), frame(frame)
{
	font = fnt;
	spanSprite = NULL;

	pal->acquire();
	palette = pal;
}

TextSpan::~TextSpan()
{
	palette->release();
	spanSprite->release();
}

const Sprite2D* TextSpan::RenderedSpan()
{
	if (!spanSprite)
		RenderSpan();
	return spanSprite;
}

void TextSpan::RenderSpan()
{
	if (spanSprite) spanSprite->release();
	// TODO: implement span alignments
	spanSprite = font->RenderTextAsSprite(text, frame, 0);
	spanSprite->acquire();
	// frame dimensions of 0 just mean size to fit
	if (frame.w == 0)
		frame.w = spanSprite->Width;
	if (frame.h == 0)
		frame.h = spanSprite->Height;
}

const Size& TextSpan::SpanFrame()
{
	if (frame.IsEmpty())
		RenderSpan(); // our true frame is determined by the rendering
	return frame;
}


TextContainer::TextContainer(const Size& frame, Font* font, Palette* pal)
	: frame(frame), font(font)
{
	pal->acquire();
	pallete = pal;
}

TextContainer::~TextContainer()
{
	SpanList::iterator it = spans.begin();
	for (; it != spans.end(); ++it) {
		delete *it;
	}
	pallete->release();
}

void TextContainer::AppendText(const String& text)
{
	AppendSpan(new TextSpan(text, font, pallete));
}

void TextContainer::AppendSpan(TextSpan* span)
{
	spans.push_back(span);
	LayoutSpansStartingAt(--spans.end());
}

void TextContainer::InsertSpanAfter(TextSpan* newSpan, const TextSpan* existing)
{
	if (!existing) { // insert at beginning;
		spans.push_front(newSpan);
		return;
	}
	SpanList::iterator it;
	it = std::find(spans.begin(), spans.end(), existing);
	spans.insert(++it, newSpan);
	LayoutSpansStartingAt(it);
}

TextSpan* TextContainer::RemoveSpan(const TextSpan* span)
{
	SpanList::iterator it;
	it = std::find(spans.begin(), spans.end(), span);
	if (it != spans.end()) {
		LayoutSpansStartingAt(--spans.erase(it));
		return (TextSpan*)span;
	}
	return NULL;
}

const TextSpan* TextContainer::SpanAtPoint(const Point& p) const
{
	// the point we are testing is relative to the container
	Region rgn = Region(0, 0, frame.w, frame.h);
	if (!rgn.PointInside(p))
		return NULL;

	SpanLayout::const_iterator it;
	for (it = layout.begin(); it != layout.end(); ++it) {
		if ((*it).second.PointInside(p)) {
			return (*it).first;
		}
	}
	return NULL;
}

void TextContainer::DrawContents(int x, int y) const
{
	Video* video = core->GetVideoDriver();
	//Region rgn = Region(Point(0,0), frame);
	SpanLayout::const_iterator it;
	for (it = layout.begin(); it != layout.end(); ++it) {
		Region rgn = (*it).second;
		rgn.x += x;
		rgn.y += y;
		video->DrawRect(rgn, ColorRed);
		video->BlitSprite((*it).first->RenderedSpan(),
						  rgn.x, rgn.y, true, &rgn);
	}
}

void TextContainer::LayoutSpansStartingAt(SpanList::const_iterator it)
{
	assert(it != spans.end());
	Point drawPoint(0, 0);
	TextSpan* span = *it;

	if (it != spans.begin()) {
		// get the last draw position
		const Region& rgn = layout[*--it];
		drawPoint.x = rgn.x + rgn.w + 1;
		drawPoint.y = rgn.y;
		it++;
	} else {
		drawPoint.y = span->SpanFrame().h;
	}

	for (; it != spans.end(); it++) {
		span = *it;
		const Size& spanFrame = span->SpanFrame();

		// FIXME: this only calculates left alignment
		// it also doesnt support block layout
		Region layoutRgn;
		const Region* excluded = NULL;
		do {
			if (excluded) {
				// we know that we have to move at least to the right
				// TODO: implement handling for block alignment
				drawPoint.x = excluded->x + excluded->w + 1;
			} else if (layoutRgn.w) { // we dont want to evaluate this the first time
				drawPoint.x += spanFrame.w + 1;
			}
			if (drawPoint.x && drawPoint.x + spanFrame.w > frame.w) {
				// move down and back
				drawPoint.x = 0;
				drawPoint.y += spanFrame.h;
			}
			layoutRgn = Region(drawPoint, spanFrame);
			excluded = ExcludedRegionForRect(layoutRgn);
		} while (excluded);

		layout[span] = layoutRgn;
		// TODO: need to extend the exclusion rect for some alignments
		// eg right align should invalidate the entire area infront also
		AddExclusionRect(layoutRgn);
	}
}

void TextContainer::AddExclusionRect(const Region& rect)
{
	assert(!rect.Dimensions().IsEmpty());
	std::vector<Region>::iterator it;
	for (it = ExclusionRects.begin(); it != ExclusionRects.end(); ++it) {
		if (rect.InsideRegion(*it)) {
			// already have an encompassing region
			break;
		} else if ((*it).InsideRegion(rect)) {
			// new region swallows the old, replace it;
			*it = rect;
			break;
		}
	}
	if (it == ExclusionRects.end()) {
		// no match found
		ExclusionRects.push_back(rect);
	}
}

const Region* TextContainer::ExcludedRegionForRect(const Region& rect)
{
	std::vector<Region>::const_iterator it;
	for (it = ExclusionRects.begin(); it != ExclusionRects.end(); ++it) {
		if (rect.IntersectsRegion(*it))
			return &*it;
	}
	return NULL;
}

}
