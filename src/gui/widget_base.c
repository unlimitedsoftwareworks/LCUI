/*
 * widget_base.c -- The widget base operation set.
 *
 * Copyright (c) 2018, Liu chao <lc-soft@live.cn> All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of LCUI nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <LCUI_Build.h>
#include <LCUI/LCUI.h>
#include <LCUI/gui/widget.h>
#include <LCUI/gui/metrics.h>

static struct LCUI_WidgetModule {
	LCUI_Widget root; /**< 根级部件 */
	LinkedList trash; /**< 待删除的部件列表 */
} LCUIWidget;

static inline float ToBorderBoxWidth(LCUI_Widget w, float content_width)
{
	return content_width + w->padding.left + w->padding.right +
	       w->computed_style.border.left.width +
	       w->computed_style.border.right.width;
}

static inline float ToBorderBoxHeight(LCUI_Widget w, float content_height)
{
	return content_height + w->padding.top + w->padding.bottom +
	       w->computed_style.border.top.width +
	       w->computed_style.border.bottom.width;
}

static inline float ToContentBoxWidth(LCUI_Widget w, float width)
{
	return width - w->padding.left - w->padding.right -
	       w->computed_style.border.left.width -
	       w->computed_style.border.right.width;
}

static inline float ToContentBoxHeight(LCUI_Widget w, float height)
{
	return height - w->padding.top - w->padding.bottom -
	       w->computed_style.border.top.width -
	       w->computed_style.border.bottom.width;
}

LCUI_Widget LCUIWidget_GetRoot(void)
{
	return LCUIWidget.root;
}

size_t LCUIWidget_ClearTrash(void)
{
	size_t count;
	LinkedListNode *node;

	node = LCUIWidget.trash.head.next;
	count = LCUIWidget.trash.length;
	while (node) {
		LinkedListNode *next = node->next;
		LinkedList_Unlink(&LCUIWidget.trash, node);
		Widget_ExecDestroy(node->data);
		node = next;
	}
	return count;
}

static void Widget_AddToTrash(LCUI_Widget w)
{
	w->state = LCUI_WSTATE_DELETED;
	if (Widget_Unlink(w) != 0) {
		return;
	}
	LinkedList_AppendNode(&LCUIWidget.trash, &w->node);
	Widget_PostSurfaceEvent(w, LCUI_WEVENT_UNLINK, TRUE);
}

/** 构造函数 */
static void Widget_Init(LCUI_Widget widget)
{
	ZEROSET(widget, LCUI_Widget);
	widget->state = LCUI_WSTATE_CREATED;
	widget->trigger = EventTrigger();
	widget->style = StyleSheet();
	widget->custom_style = StyleSheet();
	widget->inherited_style = StyleSheet();
	widget->computed_style.opacity = 1.0;
	widget->computed_style.visible = TRUE;
	widget->computed_style.focusable = FALSE;
	widget->computed_style.display = SV_BLOCK;
	widget->computed_style.position = SV_STATIC;
	widget->computed_style.pointer_events = SV_AUTO;
	widget->computed_style.box_sizing = SV_CONTENT_BOX;
	widget->computed_style.margin.top.type = LCUI_STYPE_PX;
	widget->computed_style.margin.right.type = LCUI_STYPE_PX;
	widget->computed_style.margin.bottom.type = LCUI_STYPE_PX;
	widget->computed_style.margin.left.type = LCUI_STYPE_PX;
	widget->computed_style.padding.top.type = LCUI_STYPE_PX;
	widget->computed_style.padding.right.type = LCUI_STYPE_PX;
	widget->computed_style.padding.bottom.type = LCUI_STYPE_PX;
	widget->computed_style.padding.left.type = LCUI_STYPE_PX;
	Widget_InitBackground(widget);
	LinkedList_Init(&widget->children);
	LinkedList_Init(&widget->children_show);
}

LCUI_Widget LCUIWidget_New(const char *type)
{
	ASSIGN(widget, LCUI_Widget);
	Widget_Init(widget);
	widget->node.data = widget;
	widget->node_show.data = widget;
	widget->node.next = widget->node.prev = NULL;
	widget->node_show.next = widget->node_show.prev = NULL;
	if (type) {
		widget->proto = LCUIWidget_GetPrototype(type);
		if (widget->proto) {
			widget->type = widget->proto->name;
			widget->proto->init(widget);
		} else {
			widget->type = strdup2(type);
		}
	}
	Widget_AddTask(widget, LCUI_WTASK_REFRESH_STYLE);
	return widget;
}

void Widget_ExecDestroy(LCUI_Widget w)
{
	if (w->parent) {
		Widget_UpdateLayout(w->parent);
		Widget_Unlink(w);
	}
	Widget_DestroyBackground(w);
	Widget_DestroyEventTrigger(w);
	Widget_DestroyChildren(w);
	Widget_ClearPrototype(w);
	if (w->title) {
		free(w->title);
		w->title = NULL;
	}
	Widget_DestroyId(w);
	Widget_DestroyStyleSheets(w);
	Widget_DestroyAttributes(w);
	Widget_DestroyClasses(w);
	Widget_DestroyStatus(w);
	free(w);
}

void Widget_Destroy(LCUI_Widget w)
{
	LCUI_Widget root = w;

	assert(w->state != LCUI_WSTATE_DELETED);
	while (root->parent) {
		root = root->parent;
	}
	/* If this widget is not mounted in the root widget tree */
	if (root != LCUIWidget.root) {
		w->state = LCUI_WSTATE_DELETED;
		Widget_ExecDestroy(w);
		return;
	}
	if (w->parent) {
		LCUI_Widget child;
		LinkedListNode *node;

		/* Update the index of the siblings behind it */
		node = w->node.next;
		while (node) {
			child = node->data;
			child->index -= 1;
			node = node->next;
		}
		if (w->computed_style.position != SV_ABSOLUTE) {
			Widget_UpdateLayout(w->parent);
		}
		Widget_InvalidateArea(w, NULL, SV_GRAPH_BOX);
		Widget_AddToTrash(w);
	}
}

void Widget_Empty(LCUI_Widget w)
{
	LCUI_Widget root = w;

	while (root->parent) {
		root = root->parent;
	}
	if (root == LCUIWidget.root) {
		LinkedListNode *next, *node;
		node = w->children.head.next;
		while (node) {
			next = node->next;
			Widget_AddToTrash(node->data);
			node = next;
		}
		Widget_InvalidateArea(w, NULL, SV_GRAPH_BOX);
		Widget_AddTask(w, LCUI_WTASK_LAYOUT);
	} else {
		Widget_DestroyChildren(w);
	}
}

void Widget_GetOffset(LCUI_Widget w, LCUI_Widget parent, float *offset_x,
		      float *offset_y)
{
	float x = 0, y = 0;
	while (w != parent) {
		x += w->box.border.x;
		y += w->box.border.y;
		w = w->parent;
		if (w) {
			x += w->box.padding.x - w->box.border.x;
			y += w->box.padding.y - w->box.border.y;
		} else {
			break;
		}
	}
	*offset_x = x;
	*offset_y = y;
}

int Widget_Top(LCUI_Widget w)
{
	return Widget_Append(LCUIWidget.root, w);
}

void Widget_SetTitleW(LCUI_Widget w, const wchar_t *title)
{
	size_t len;
	wchar_t *new_title, *old_title;

	len = wcslen(title) + 1;
	new_title = (wchar_t *)malloc(sizeof(wchar_t) * len);
	if (!new_title) {
		return;
	}
	wcsncpy(new_title, title, len);
	old_title = w->title;
	w->title = new_title;
	if (old_title) {
		free(old_title);
	}
	Widget_AddTask(w, LCUI_WTASK_TITLE);
}

void Widget_AddState(LCUI_Widget w, LCUI_WidgetState state)
{
	/* 如果部件还处于未准备完毕的状态 */
	if (w->state < LCUI_WSTATE_READY) {
		w->state |= state;
		/* 如果部件已经准备完毕则触发 ready 事件 */
		if (w->state == LCUI_WSTATE_READY) {
			LCUI_WidgetEventRec e = { 0 };
			e.type = LCUI_WEVENT_READY;
			e.cancel_bubble = TRUE;
			Widget_TriggerEvent(w, &e, NULL);
			w->state = LCUI_WSTATE_NORMAL;
		}
	}
}

static float ComputeXMetric(LCUI_Widget w, int key)
{
	LCUI_Style s = &w->style->sheet[key];
	if (s->type == LCUI_STYPE_SCALE) {
		if (!w->parent) {
			return 0;
		}
		if (Widget_HasAbsolutePosition(w)) {
			return w->parent->box.padding.width * s->scale;
		}
		return w->parent->box.content.width * s->scale;
	}
	return LCUIMetrics_Compute(s->value, s->type);
}

static float ComputeYMetric(LCUI_Widget w, int key)
{
	LCUI_Style s = &w->style->sheet[key];
	if (s->type == LCUI_STYPE_SCALE) {
		if (!w->parent) {
			return 0;
		}
		if (Widget_HasAbsolutePosition(w)) {
			return w->parent->box.padding.height * s->scale;
		}
		return w->parent->box.content.height * s->scale;
	}
	return LCUIMetrics_Compute(s->value, s->type);
}

static int ComputeStyleOption(LCUI_Widget w, int key, int default_value)
{
	if (!w->style->sheet[key].is_valid) {
		return default_value;
	}
	if (w->style->sheet[key].type != LCUI_STYPE_STYLE) {
		return default_value;
	}
	return w->style->sheet[key].style;
}

void Widget_UpdateVisibility(LCUI_Widget w)
{
	LCUI_Style s = &w->style->sheet[key_visibility];
	LCUI_BOOL visible = w->computed_style.visible;

	if (w->computed_style.display == SV_NONE) {
		w->computed_style.visible = FALSE;
	} else if (s->is_valid && s->type == LCUI_STYPE_STRING &&
		   strcmp(s->val_string, "hidden") == 0) {
		w->computed_style.visible = FALSE;
	} else {
		w->computed_style.visible = TRUE;
	}
	if (visible == w->computed_style.visible) {
		return;
	}
	if (w->parent) {
		Widget_InvalidateArea(w, NULL, SV_GRAPH_BOX);
	}
	visible = w->computed_style.visible;
	if (visible) {
		Widget_PostSurfaceEvent(w, LCUI_WEVENT_SHOW, TRUE);
	} else {
		Widget_PostSurfaceEvent(w, LCUI_WEVENT_HIDE, TRUE);
	}
}

void Widget_UpdateDisplay(LCUI_Widget w)
{
	int display = w->computed_style.display;
	LCUI_Style s = &w->style->sheet[key_display];
	if (s->is_valid && s->type == LCUI_STYPE_STYLE) {
		w->computed_style.display = s->style;
	} else {
		w->computed_style.display = SV_BLOCK;
	}
	if (w->computed_style.display == display) {
		return;
	}
	if (w->parent && w->computed_style.position != SV_ABSOLUTE) {
		Widget_UpdateLayout(w->parent);
	}
	Widget_UpdateVisibility(w);
	Widget_UpdateLayout(w);
}

void Widget_UpdateOpacity(LCUI_Widget w)
{
	float opacity = 1.0;
	LCUI_Style s = &w->style->sheet[key_opacity];
	if (s->is_valid) {
		switch (s->type) {
		case LCUI_STYPE_INT:
			opacity = 1.0f * s->val_int;
			break;
		case LCUI_STYPE_SCALE:
			opacity = s->val_scale;
			break;
		default:
			opacity = 1.0f;
			break;
		}
		if (opacity > 1.0) {
			opacity = 1.0;
		} else if (opacity < 0.0) {
			opacity = 0.0;
		}
	}
	w->computed_style.opacity = opacity;
	Widget_InvalidateArea(w, NULL, SV_GRAPH_BOX);
}

void Widget_UpdateZIndex(LCUI_Widget w)
{
	Widget_AddTask(w, LCUI_WTASK_ZINDEX);
}

void Widget_ExecUpdateZIndex(LCUI_Widget w)
{
	int z_index;
	LinkedList *list;
	LinkedListNode *cnode, *csnode, *snode;
	LCUI_Style s = &w->style->sheet[key_z_index];
	if (s->is_valid && s->type == LCUI_STYPE_INT) {
		z_index = s->val_int;
	} else {
		z_index = 0;
	}
	if (!w->parent) {
		return;
	}
	if (w->state == LCUI_WSTATE_NORMAL) {
		if (w->computed_style.z_index == z_index) {
			return;
		}
	}
	w->computed_style.z_index = z_index;
	snode = &w->node_show;
	list = &w->parent->children_show;
	LinkedList_Unlink(list, snode);
	for (LinkedList_Each(cnode, list)) {
		LCUI_Widget child = cnode->data;
		LCUI_WidgetStyle *ccs = &child->computed_style;

		csnode = &child->node_show;
		if (w->computed_style.z_index < ccs->z_index) {
			continue;
		} else if (w->computed_style.z_index == ccs->z_index) {
			if (w->computed_style.position == ccs->position) {
				if (w->index < child->index) {
					continue;
				}
			} else if (w->computed_style.position < ccs->position) {
				continue;
			}
		}
		LinkedList_Link(list, csnode->prev, snode);
		break;
	}
	if (!cnode) {
		LinkedList_AppendNode(list, snode);
	}
	if (w->computed_style.position != SV_STATIC) {
		Widget_AddTask(w, LCUI_WTASK_REFRESH);
	}
}

/** 清除已计算的尺寸 */
void Widget_ClearComputedSize(LCUI_Widget w)
{
	if (Widget_HasAutoStyle(w, key_width)) {
		w->width = 0;
		w->box.canvas.width = 0;
		w->box.content.width = 0;
	}
	if (Widget_HasAutoStyle(w, key_height)) {
		w->height = 0;
		w->box.canvas.height = 0;
		w->box.content.height = 0;
	}
}

static void Widget_UpdateChildrenSize(LCUI_Widget w)
{
	LinkedListNode *node;
	for (LinkedList_Each(node, &w->children)) {
		LCUI_Widget child = node->data;
		LCUI_StyleSheet s = child->style;
		if (Widget_HasFillAvailableWidth(child)) {
			Widget_AddTask(child, LCUI_WTASK_RESIZE);
		} else if (Widget_HasScaleSize(child)) {
			Widget_AddTask(child, LCUI_WTASK_RESIZE);
		}
		if (Widget_HasAbsolutePosition(child)) {
			if (s->sheet[key_right].is_valid ||
			    s->sheet[key_bottom].is_valid ||
			    CheckStyleType(s, key_left, scale) ||
			    CheckStyleType(s, key_top, scale)) {
				Widget_AddTask(child, LCUI_WTASK_POSITION);
			}
		}
		if (Widget_HasAutoStyle(child, key_margin_left) ||
		    Widget_HasAutoStyle(child, key_margin_right)) {
			Widget_AddTask(child, LCUI_WTASK_MARGIN);
		}
		if (child->computed_style.vertical_align != SV_TOP) {
			Widget_AddTask(child, LCUI_WTASK_POSITION);
		}
	}
}

void Widget_UpdatePosition(LCUI_Widget w)
{
	LCUI_RectF rect;
	int position = ComputeStyleOption(w, key_position, SV_STATIC);
	int valign = ComputeStyleOption(w, key_vertical_align, SV_TOP);
	w->computed_style.vertical_align = valign;
	w->computed_style.left = ComputeXMetric(w, key_left);
	w->computed_style.right = ComputeXMetric(w, key_right);
	w->computed_style.top = ComputeYMetric(w, key_top);
	w->computed_style.bottom = ComputeYMetric(w, key_bottom);
	if (w->parent && w->computed_style.position != position) {
		w->computed_style.position = position;
		Widget_UpdateLayout(w->parent);
		Widget_ClearComputedSize(w);
		Widget_UpdateChildrenSize(w);
		/* 当部件尺寸是按百分比动态计算的时候需要重新计算尺寸 */
		if (Widget_CheckStyleType(w, key_width, scale) ||
		    Widget_CheckStyleType(w, key_height, scale)) {
			Widget_UpdateSize(w);
		}
	}
	w->computed_style.position = position;
	Widget_UpdateZIndex(w);
	rect = w->box.canvas;
	w->x = w->origin_x;
	w->y = w->origin_y;
	switch (position) {
	case SV_ABSOLUTE:
		w->x = w->y = 0;
		if (!Widget_HasAutoStyle(w, key_left)) {
			w->x = w->computed_style.left;
		} else if (!Widget_HasAutoStyle(w, key_right)) {
			if (w->parent) {
				w->x = w->parent->box.border.width;
				w->x -= w->width;
			}
			w->x -= w->computed_style.right;
		}
		if (!Widget_HasAutoStyle(w, key_top)) {
			w->y = w->computed_style.top;
		} else if (!Widget_HasAutoStyle(w, key_bottom)) {
			if (w->parent) {
				w->y = w->parent->box.border.height;
				w->y -= w->height;
			}
			w->y -= w->computed_style.bottom;
		}
		break;
	case SV_RELATIVE:
		if (!Widget_HasAutoStyle(w, key_left)) {
			w->x += w->computed_style.left;
		} else if (!Widget_HasAutoStyle(w, key_right)) {
			w->x -= w->computed_style.right;
		}
		if (!Widget_HasAutoStyle(w, key_top)) {
			w->y += w->computed_style.top;
		} else if (!Widget_HasAutoStyle(w, key_bottom)) {
			w->y -= w->computed_style.bottom;
		}
	default:
		if (w->parent) {
			w->x += w->parent->padding.left;
			w->y += w->parent->padding.top;
		}
		break;
	}
	switch (valign) {
	case SV_MIDDLE:
		if (!w->parent) {
			break;
		}
		w->y += (w->parent->box.content.height - w->height) / 2;
		break;
	case SV_BOTTOM:
		if (!w->parent) {
			break;
		}
		w->y += w->parent->box.content.height - w->height;
	case SV_TOP:
	default:
		break;
	}
	w->box.outer.x = w->x;
	w->box.outer.y = w->y;
	w->x += w->margin.left;
	w->y += w->margin.top;
	/* 以x、y为基础 */
	w->box.padding.x = w->x;
	w->box.padding.y = w->y;
	w->box.border.x = w->x;
	w->box.border.y = w->y;
	w->box.canvas.x = w->x;
	w->box.canvas.y = w->y;
	/* 计算各个框的坐标 */
	w->box.padding.x += w->computed_style.border.left.width;
	w->box.padding.y += w->computed_style.border.top.width;
	w->box.content.x = w->box.padding.x + w->padding.left;
	w->box.content.y = w->box.padding.y + w->padding.top;
	w->box.canvas.x -= Widget_GetBoxShadowOffsetX(w);
	w->box.canvas.y -= Widget_GetBoxShadowOffsetY(w);
	if (w->parent) {
		/* 标记移动前后的区域 */
		Widget_InvalidateArea(w, NULL, SV_GRAPH_BOX);
		Widget_InvalidateArea(w->parent, &rect, SV_PADDING_BOX);
	}
	/* 检测是否为顶级部件并做相应处理 */
	Widget_PostSurfaceEvent(w, LCUI_WEVENT_MOVE, TRUE);
}

static void Widget_UpdateCanvasBox(LCUI_Widget w)
{
	LCUI_RectF *rg = &w->box.canvas;
	rg->x = w->x - Widget_GetBoxShadowOffsetX(w);
	rg->y = w->y - Widget_GetBoxShadowOffsetY(w);
	rg->width = Widget_GetCanvasWidth(w);
	rg->height = Widget_GetCanvasHeight(w);
}

static LCUI_BOOL Widget_ComputeStaticSize(LCUI_Widget w, float *width,
					  float *height)
{
	LCUI_WidgetBoxModelRec *box = &w->box;
	LCUI_WidgetStyle *style = &w->computed_style;

	/* If it is non-static layout */
	if (style->display == SV_NONE || style->position == SV_ABSOLUTE) {
		return FALSE;
	}
	if (Widget_HasParentDependentWidth(w)) {
		/* Compute the full size of child widget */
		Widget_AutoSize(w);
		/* Recompute the actual size of child widget later */
		Widget_AddTask(w, LCUI_WTASK_RESIZE);
	}
	/* If its width is a percentage, it ignores the effect of its outer
	 * and inner spacing on the static width. */
	if (Widget_CheckStyleType(w, key_width, scale)) {
		if (style->box_sizing == SV_BORDER_BOX) {
			*width = box->border.x + box->border.width;
		} else {
			*width = box->content.x + box->content.width;
			*width -= box->content.x - box->border.x;
		}
		*width -= box->outer.x - box->border.x;
	} else if (box->outer.width <= 0) {
		return FALSE;
	} else {
		*width = box->outer.x + box->outer.width;
	}
	if (Widget_CheckStyleType(w, key_height, scale)) {
		if (style->box_sizing == SV_BORDER_BOX) {
			*height = box->border.y + box->border.height;
		} else {
			*height = box->content.y + box->content.height;
			*height -= box->content.y - box->border.y;
		}
		*height -= box->outer.y - box->border.y;
	} else if (box->outer.height <= 0) {
		return FALSE;
	} else {
		*height = box->outer.y + box->outer.height;
	}
	return TRUE;
}

static void Widget_ComputeStaticContentSize(LCUI_Widget w, float *out_width,
					    float *out_height)
{
	LinkedListNode *node;
	float content_width = 0, content_height = 0, width, height;

	for (LinkedList_Each(node, &w->children_show)) {
		if (!Widget_ComputeStaticSize(node->data, &width, &height)) {
			continue;
		}
		content_width = max(content_width, width);
		content_height = max(content_height, height);
	}
	/* The child widget's coordinates are relative to the padding box,
	 * not the content box, so it needs to be converted */
	content_width -= w->padding.left;
	content_height -= w->padding.top;
	if (out_width && *out_width <= 0) {
		*out_width = content_width;
	}
	if (out_height && *out_height <= 0) {
		*out_height = content_height;
	}
}
LCUI_BOOL Widget_HasAutoStyle(LCUI_Widget w, int key)
{
	return !Widget_CheckStyleValid(w, key) ||
	       Widget_CheckStyleType(w, key, AUTO);
}

LCUI_BOOL Widget_HasStaticWidthParent(LCUI_Widget widget)
{
	LCUI_Widget w;
	for (w = widget->parent; w; w = w->parent) {
		if (w->computed_style.max_width >= 0) {
			return TRUE;
		}
		if (!Widget_HasAutoStyle(w, key_width)) {
			return TRUE;
		}
		if (Widget_HasAbsolutePosition(w) ||
		    Widget_HasInlineBlockDisplay(w)) {
			return FALSE;
		}
	}
	return FALSE;
}

LCUI_BOOL Widget_HasFitContentWidth(LCUI_Widget w)
{
	if (!Widget_HasAutoStyle(w, key_width)) {
		return FALSE;
	}
	if (Widget_HasInlineBlockDisplay(w)) {
		return TRUE;
	}
	if (Widget_HasAbsolutePosition(w) || !Widget_HasStaticWidthParent(w)) {
		return TRUE;
	}
	return FALSE;
}

/** 根据当前部件的内外间距，获取调整后宽度 */
static float Widget_GetAdjustedWidth(LCUI_Widget w, float width)
{
	/* 如果部件的宽度不具备自动填满剩余空间的特性，则不调整 */
	if (!Widget_HasFillAvailableWidth(w)) {
		return width;
	}
	if (!Widget_HasAutoStyle(w, key_margin_left)) {
		width -= w->margin.left;
	}
	if (!Widget_HasAutoStyle(w, key_margin_right)) {
		width -= w->margin.right;
	}
	return width;
}

float Widget_ComputeFillAvailableWidth(LCUI_Widget w)
{
	float width;
	width = Widget_ComputeMaxAvaliableWidth(w);
	return Widget_GetAdjustedWidth(w, width);
}

void Widget_ComputeLimitSize(LCUI_Widget w)
{
	LCUI_WidgetStyle *style;
	style = &w->computed_style;
	style->max_width = -1;
	style->min_width = -1;
	style->max_height = -1;
	style->min_height = -1;
	if (Widget_CheckStyleValid(w, key_max_width)) {
		style->max_width = ComputeXMetric(w, key_max_width);
	}
	if (Widget_CheckStyleValid(w, key_min_width)) {
		style->min_width = ComputeXMetric(w, key_min_width);
	}
	if (Widget_CheckStyleValid(w, key_max_height)) {
		style->max_height = ComputeYMetric(w, key_max_height);
	}
	if (Widget_CheckStyleValid(w, key_min_height)) {
		style->min_height = ComputeYMetric(w, key_min_height);
	}
}

float Widget_GetLimitedWidth(LCUI_Widget w, float width)
{
	LCUI_WidgetStyle *style;
	style = &w->computed_style;
	if (style->max_width > -1 && width > style->max_width) {
		width = style->max_width;
	}
	if (style->min_width > -1 && width < style->min_width) {
		width = style->min_width;
	}
	return width;
}

float Widget_GetLimitedHeight(LCUI_Widget w, float height)
{
	LCUI_WidgetStyle *style;
	style = &w->computed_style;
	if (style->max_height > -1 && height > style->max_height) {
		height = style->max_height;
	}
	if (style->min_height > -1 && height < style->min_height) {
		height = style->min_height;
	}
	return height;
}

static void Widget_SetSize(LCUI_Widget w, float width, float height)
{
	LCUI_RectF *box, *pbox;
	LCUI_BorderStyle *bbox;

	w->width = width;
	w->height = height;
	w->width = Widget_GetLimitedWidth(w, width);
	w->height = Widget_GetLimitedHeight(w, height);
	w->box.border.width = w->width;
	w->box.border.height = w->height;
	w->box.content.width = w->width;
	w->box.content.height = w->height;
	w->box.padding.width = w->width;
	w->box.padding.height = w->height;
	pbox = &w->box.padding;
	bbox = &w->computed_style.border;
	/* 如果是以边框盒作为尺寸调整对象，则需根据边框盒计算内容框尺寸 */
	if (w->computed_style.box_sizing == SV_BORDER_BOX) {
		box = &w->box.content;
		pbox->width -= bbox->left.width + bbox->right.width;
		pbox->height -= bbox->top.width + bbox->bottom.width;
		box->width = pbox->width;
		box->height = pbox->height;
		box->width -= w->padding.left + w->padding.right;
		box->height -= w->padding.top + w->padding.bottom;
	} else {
		/* 否则是以内容框作为尺寸调整对象，需计算边框盒的尺寸 */
		box = &w->box.border;
		pbox->width += w->padding.left + w->padding.right;
		pbox->height += w->padding.top + w->padding.bottom;
		box->width = pbox->width;
		box->height = pbox->height;
		box->width += bbox->left.width + bbox->right.width;
		box->height += bbox->top.width + bbox->bottom.width;
	}
	w->width = w->box.border.width;
	w->height = w->box.border.height;
	w->box.outer.width = w->box.border.width;
	w->box.outer.height = w->box.border.height;
	w->box.outer.width += w->margin.left + w->margin.right;
	w->box.outer.height += w->margin.top + w->margin.bottom;
	Widget_UpdateCanvasBox(w);
}

void Widget_ComputeContentSize(LCUI_Widget w, float *width, float *height)
{
	float limited_width;
	float static_width = 0, static_height = 0;
	float content_width = width ? *width : 0;
	float content_height = height ? *height : 0;

	Widget_ComputeLimitSize(w);
	Widget_ComputeStaticContentSize(w, &static_width, &static_height);
	if (w->proto && w->proto->autosize) {
		w->proto->autosize(w, &content_width, &content_height);
	}
	if (width && *width > 0) {
		goto done;
	}
	content_width = max(content_width, static_width);
	if (w->computed_style.box_sizing == SV_BORDER_BOX) {
		content_width = ToBorderBoxWidth(w, content_width);
	}
	limited_width = Widget_GetLimitedWidth(w, content_width);
	if (limited_width != content_width) {
		content_height = 0;
		content_width = limited_width;
		if (w->computed_style.box_sizing == SV_BORDER_BOX) {
			content_width = ToContentBoxWidth(w, content_width);
		}
		if (w->proto && w->proto->autosize) {
			w->proto->autosize(w, &content_width, &content_height);
		}
	} else {
		if (w->computed_style.box_sizing == SV_BORDER_BOX) {
			content_width = ToContentBoxWidth(w, content_width);
		}
	}

done:
	content_height = max(content_height, static_height);
	if (width && *width <= 0) {
		*width = content_width;
	}
	if (height && *height <= 0) {
		*height = content_height;
	}
}

void Widget_AutoSize(LCUI_Widget w)
{
	float width = 0, height = 0;
	if (!Widget_CheckStyleType(w, key_width, scale)) {
		width = ComputeXMetric(w, key_width);
	}
	if (!Widget_CheckStyleType(w, key_height, scale)) {
		height = ComputeYMetric(w, key_height);
	}
	Widget_ComputeContentSize(w, &width, &height);
	width = Widget_GetAdjustedWidth(w, width);
	Widget_SetSize(w, ToBorderBoxWidth(w, width),
		       ToBorderBoxHeight(w, height));
}

static void Widget_ComputeSize(LCUI_Widget w)
{
	float width, height;
	float max_width = -1, default_width = -1;

	Widget_ComputeLimitSize(w);
	width = ComputeXMetric(w, key_width);
	height = ComputeYMetric(w, key_height);
	if (width > 0) {
		width = Widget_GetLimitedWidth(w, width);
	}
	if (height > 0) {
		height = Widget_GetLimitedHeight(w, height);
	}
	if (!Widget_HasAutoStyle(w, key_width) &&
	    !Widget_HasAutoStyle(w, key_height)) {
		if (w->computed_style.box_sizing == SV_CONTENT_BOX) {
			width = ToBorderBoxWidth(w, width);
			height = ToBorderBoxHeight(w, height);
		}
		Widget_SetSize(w, width, height);
		return;
	}
	if (w->computed_style.box_sizing == SV_BORDER_BOX) {
		width = ToContentBoxWidth(w, width);
		height = ToContentBoxHeight(w, height);
	}
	if (Widget_HasAutoStyle(w, key_width) &&
	    Widget_HasFillAvailableWidth(w)) {
		width = Widget_ComputeFillAvailableWidth(w);
		width = ToContentBoxWidth(w, width);
		if (!Widget_HasStaticWidthParent(w)) {
			default_width = w->parent->box.content.width;
			if (w->computed_style.box_sizing == SV_BORDER_BOX) {
				default_width =
				    ToContentBoxWidth(w, default_width);
			}
			max_width = width;
			width = 0;
		}
	}
	Widget_ComputeContentSize(w, &width, &height);
	if (default_width != -1 && width < default_width) {
		width = default_width;
	}
	if (max_width != -1 && width > max_width) {
		width = max_width;
	}
	if (w->computed_style.box_sizing == SV_BORDER_BOX) {
		width = ToBorderBoxWidth(w, width);
		height = ToBorderBoxHeight(w, height);
	}
	Widget_SetSize(w, width, height);
}

static void Widget_SendResizeEvent(LCUI_Widget w)
{
	LCUI_WidgetEventRec e;
	e.target = w;
	e.data = NULL;
	e.type = LCUI_WEVENT_RESIZE;
	e.cancel_bubble = TRUE;
	Widget_TriggerEvent(w, &e, NULL);
	Widget_AddTask(w, LCUI_WTASK_REFRESH);
	Widget_PostSurfaceEvent(w, LCUI_WEVENT_RESIZE, FALSE);
}

void Widget_UpdateMargin(LCUI_Widget w)
{
	int i;
	LCUI_BoundBox *mbox = &w->computed_style.margin;
	struct {
		LCUI_Style sval;
		float *fval;
		int key;
	} pd_map[4] = { { &mbox->top, &w->margin.top, key_margin_top },
			{ &mbox->right, &w->margin.right, key_margin_right },
			{ &mbox->bottom, &w->margin.bottom, key_margin_bottom },
			{ &mbox->left, &w->margin.left, key_margin_left } };
	for (i = 0; i < 4; ++i) {
		LCUI_Style s = &w->style->sheet[pd_map[i].key];
		if (!s->is_valid) {
			pd_map[i].sval->type = LCUI_STYPE_PX;
			pd_map[i].sval->px = 0.0;
			*pd_map[i].fval = 0.0;
			continue;
		}
		*pd_map[i].sval = *s;
		*pd_map[i].fval = LCUIMetrics_Compute(s->value, s->type);
	}
	/* 如果有父级部件，则处理 margin-left 和 margin-right 的值 */
	if (w->parent) {
		float width = w->parent->box.content.width;
		if (Widget_HasAutoStyle(w, key_margin_left)) {
			if (Widget_HasAutoStyle(w, key_margin_right)) {
				w->margin.left = (width - w->width) / 2;
				if (w->margin.left < 0) {
					w->margin.left = 0;
				}
				w->margin.right = w->margin.left;
			} else {
				w->margin.left = width - w->width;
				w->margin.left -= w->margin.right;
				if (w->margin.left < 0) {
					w->margin.left = 0;
				}
			}
		} else if (Widget_HasAutoStyle(w, key_margin_right)) {
			w->margin.right = width - w->width;
			w->margin.right -= w->margin.left;
			if (w->margin.right < 0) {
				w->margin.right = 0;
			}
		}
	}
	if (w->parent) {
		if (Widget_HasAutoStyle(w->parent, key_width) ||
		    Widget_HasAutoStyle(w->parent, key_height)) {
			Widget_AddTask(w->parent, LCUI_WTASK_RESIZE);
		}
		if (w->computed_style.display != SV_NONE &&
		    w->computed_style.position == SV_STATIC) {
			Widget_UpdateLayout(w->parent);
		}
	}
	Widget_AddTask(w, LCUI_WTASK_POSITION);
}

void Widget_ComputePadding(LCUI_Widget w)
{
	int i;
	LCUI_BoundBox *pbox = &w->computed_style.padding;
	struct {
		LCUI_Style sval;
		float *ival;
		int key;
	} pd_map[4] = { { &pbox->top, &w->padding.top, key_padding_top },
			{ &pbox->right, &w->padding.right, key_padding_right },
			{ &pbox->bottom, &w->padding.bottom,
			  key_padding_bottom },
			{ &pbox->left, &w->padding.left, key_padding_left } };
	/* 内边距的单位暂时都用 px  */
	for (i = 0; i < 4; ++i) {
		LCUI_Style s = &w->style->sheet[pd_map[i].key];
		if (!s->is_valid) {
			pd_map[i].sval->type = LCUI_STYPE_PX;
			pd_map[i].sval->px = 0.0;
			*pd_map[i].ival = 0.0;
			continue;
		}
		*pd_map[i].sval = *s;
		*pd_map[i].ival = LCUIMetrics_Compute(s->value, s->type);
	}
}

void Widget_UpdateSize(LCUI_Widget w)
{
	int box_sizing;
	LCUI_RectF rect = w->box.canvas;
	LCUI_Rect2F padding = w->padding;

	box_sizing = ComputeStyleOption(w, key_box_sizing, SV_CONTENT_BOX);
	w->computed_style.box_sizing = box_sizing;
	Widget_ComputePadding(w);
	Widget_ComputeSize(w);
	/* 如果左右外间距是 auto 类型的，则需要计算外间距 */
	if (w->style->sheet[key_margin_left].is_valid &&
	    w->style->sheet[key_margin_left].type == LCUI_STYPE_AUTO) {
		Widget_UpdateMargin(w);
	} else if (w->style->sheet[key_margin_right].is_valid &&
		   w->style->sheet[key_margin_right].type == LCUI_STYPE_AUTO) {
		Widget_UpdateMargin(w);
	}
	/* 若尺寸无变化则不继续处理 */
	if (rect.width == w->box.canvas.width &&
	    rect.height == w->box.canvas.height &&
	    padding.top == w->padding.top &&
	    padding.right == w->padding.right &&
	    padding.bottom == w->padding.bottom &&
	    padding.left == w->padding.left) {
		return;
	}
	/* 若在变化前后的宽高中至少有一个为 0，则不继续处理 */
	if ((w->box.canvas.width <= 0 || w->box.canvas.height <= 0) &&
	    (rect.width <= 0 || rect.height <= 0)) {
		return;
	}
	Widget_UpdateLayout(w);
	/* 如果垂直对齐方式不为顶部对齐 */
	if (w->computed_style.vertical_align != SV_TOP) {
		Widget_UpdatePosition(w);
	} else if (w->computed_style.position == SV_ABSOLUTE) {
		/* 如果是绝对定位，且指定了右间距或底间距 */
		if (!Widget_HasAutoStyle(w, key_right) ||
		    !Widget_HasAutoStyle(w, key_bottom)) {
			Widget_UpdatePosition(w);
		}
	}
	if (w->parent) {
		/* Mark old area and new area need to repaint */
		Widget_InvalidateArea(w->parent, &rect, SV_PADDING_BOX);
		rect.width = w->box.canvas.width;
		rect.height = w->box.canvas.height;
		Widget_InvalidateArea(w->parent, &rect, SV_PADDING_BOX);
		/* If its width depends on the parent, there is no need to
		 * repeatedly update the parent size and layout.
		 * See Widget_ComputeStaticSize() for more details. */
		if (!Widget_HasParentDependentWidth(w)) {
			if (Widget_HasAutoStyle(w->parent, key_width) ||
			    Widget_HasAutoStyle(w->parent, key_height)) {
				Widget_AddTask(w->parent, LCUI_WTASK_RESIZE);
			}
			if (w->computed_style.display != SV_NONE &&
			    w->computed_style.position == SV_STATIC) {
				Widget_UpdateLayout(w->parent);
			}
		}
	}
	Widget_SendResizeEvent(w);
	Widget_UpdateChildrenSize(w);
}

void Widget_UpdateSizeWithSurface(LCUI_Widget w)
{
	Widget_UpdateSize(w);
	Widget_PostSurfaceEvent(w, LCUI_WEVENT_RESIZE, TRUE);
}

void Widget_UpdateProps(LCUI_Widget w)
{
	LCUI_Style s;
	int prop = ComputeStyleOption(w, key_pointer_events, SV_AUTO);
	w->computed_style.pointer_events = prop;
	s = &w->style->sheet[key_focusable];
	if (s->is_valid && s->type == LCUI_STYPE_BOOL && s->value == 0) {
		w->computed_style.focusable = FALSE;
	} else {
		w->computed_style.focusable = TRUE;
	}
}

float Widget_ComputeMaxAvaliableWidth(LCUI_Widget widget)
{
	LCUI_Widget w;
	float width = 0, padding = 0, margin = 0;

	for (w = widget->parent; w; w = w->parent) {
		if (!Widget_HasAutoStyle(w, key_width) ||
		    w->computed_style.max_width >= 0) {
			width = w->box.content.width;
			break;
		}
		if (Widget_HasFillAvailableWidth(w)) {
			margin += w->box.outer.width - w->box.border.width;
		}
		padding += w->box.border.width - w->box.content.width;
	}
	width -= padding + margin;
	if (Widget_HasAbsolutePosition(widget)) {
		width += widget->padding.left + widget->padding.right;
	}
	if (width < 0) {
		width = 0;
	}
	return width;
}

float Widget_ComputeMaxWidth(LCUI_Widget widget)
{
	float width;
	if (!Widget_HasAutoStyle(widget, key_width) &&
	    !Widget_HasParentDependentWidth(widget)) {
		return widget->box.border.width;
	}
	width = Widget_ComputeMaxAvaliableWidth(widget);
	if (!Widget_HasAutoStyle(widget, key_max_width)) {
		if (widget->computed_style.max_width > -1 &&
		    width < widget->computed_style.max_width) {
			width = widget->computed_style.max_width;
		}
	}
	return width;
}

float Widget_ComputeMaxContentWidth(LCUI_Widget w)
{
	float width = Widget_ComputeMaxWidth(w);
	width = Widget_GetAdjustedWidth(w, width);
	return ToContentBoxWidth(w, width);
}

void LCUIWidget_InitBase(void)
{
	LinkedList_Init(&LCUIWidget.trash);
	LCUIWidget.root = LCUIWidget_New("root");
	Widget_SetTitleW(LCUIWidget.root, L"LCUI Display");
}

void LCUIWidget_FreeRoot(void)
{
	Widget_ExecDestroy(LCUIWidget.root);
}

void LCUIWidget_FreeBase(void)
{
	LCUIWidget.root = NULL;
}
