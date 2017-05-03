// Copyright 2016 Cheng Zhao. All rights reserved.
// Use of this source code is governed by the license that can be found in the
// LICENSE file.

#include "nativeui/window.h"

#include <gtk/gtk.h>

#include "nativeui/menu_bar.h"

namespace nu {

namespace {

// User clicks the close button.
gboolean OnClose(GtkWidget* widget, GdkEvent* event, Window* window) {
  if (window->should_close.is_null() || window->should_close.Run())
    window->Close();

  // We are destroying the window ourselves, so prevent the default behavior.
  return TRUE;
}

// Force window to allocate size for content view.
void ForceSizeAllocation(GtkWindow* window, GtkWidget* view) {
  GdkRectangle rect = { 0, 0 };
  gtk_window_get_size(window, &rect.width, &rect.height);
  gtk_widget_size_allocate(view, &rect);
}

// Resize a window ignoring the size request.
void ResizeWindow(Window* window, bool resizable, int width, int height) {
  // Clear current size requests.
  GtkWindow* gwin = window->GetNative();
  GtkWidget* vbox = gtk_bin_get_child(GTK_BIN(gwin));
  gtk_widget_set_size_request(GTK_WIDGET(gwin), -1, -1);
  gtk_widget_set_size_request(vbox, -1, -1);

  if (resizable || window->HasFrame()) {
    // gtk_window_resize only works for resizable window.
    if (resizable)
      gtk_window_resize(gwin, width, height);
    else
      gtk_widget_set_size_request(GTK_WIDGET(gwin), width, height);
  } else {
    // Setting size request on the window results in weird behavior for
    // unresizable CSD windows, probably related to size of shadows.
    gtk_widget_set_size_request(vbox, width, height);
  }

  // Set default size otherwise GTK may do weird things when setting size
  // request or changing resizable property.
  gtk_window_set_default_size(gwin, width, height);

  // Notify the content view of the resize.
  ForceSizeAllocation(gwin, vbox);
}

}  // namespace

void Window::PlatformInit(const Options& options) {
  window_ = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_focus_on_map(window_, false);

  if (!options.frame) {
    // Setting a hidden titlebar to force using CSD rendering.
    gtk_window_set_titlebar(window_, gtk_label_new("you should not see me"));

    // Since we are not using any titlebar, we have to override the border
    // radius of the client decoration to avoid having rounded shadow for the
    // rectange window.
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        provider,
        // Using 0 would triger a bug of GTK's shadow rendering code.
        "decoration { border-radius: 0.01px; }", -1, nullptr);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(GTK_WIDGET(window_)),
        GTK_STYLE_PROVIDER(provider), G_MAXUINT);
    // Store the provider inside window, we may need to remove it later when
    // user sets a custom titlebar.
    g_object_set_data_full(G_OBJECT(window_), "", provider, g_object_unref);
  }

  // Must use a vbox to pack menubar.
  GtkWidget* vbox = gtk_vbox_new(FALSE, 0);
  gtk_widget_show(vbox);
  gtk_container_add(GTK_CONTAINER(window_), vbox);

  if (!options.bounds.IsEmpty()) {
    gtk_window_set_default_size(window_, options.bounds.width(),
                                         options.bounds.height());
    gtk_window_move(window_, options.bounds.x(), options.bounds.y());
  }

  g_signal_connect(window_, "delete-event", G_CALLBACK(OnClose), this);
}

void Window::PlatformDestroy() {
  if (window_)
    gtk_widget_destroy(GTK_WIDGET(window_));
}

void Window::Close() {
  if (!should_close.is_null() && !should_close.Run())
    return;

  on_close.Emit();
  gtk_widget_destroy(GTK_WIDGET(window_));

  window_ = nullptr;
}

void Window::PlatformSetContentView(View* view) {
  GtkContainer* vbox = GTK_CONTAINER(gtk_bin_get_child(GTK_BIN(window_)));
  if (content_view_)
    gtk_container_remove(vbox, content_view_->GetNative());
  gtk_container_add(vbox, view->GetNative());
  gtk_box_set_child_packing(GTK_BOX(vbox), view->GetNative(), TRUE, TRUE,
                            0, GTK_PACK_END);

  ForceSizeAllocation(window_, GTK_WIDGET(vbox));
}

void Window::PlatformSetMenu(MenuBar* menu_bar) {
  GtkContainer* vbox = GTK_CONTAINER(gtk_bin_get_child(GTK_BIN(window_)));
  if (menu_bar_)
    gtk_container_remove(vbox, GTK_WIDGET(menu_bar_->GetNative()));
  GtkWidget* menu = GTK_WIDGET(menu_bar->GetNative());
  gtk_container_add(vbox, menu);
  gtk_box_set_child_packing(GTK_BOX(vbox), menu, FALSE, FALSE, 0,
                            GTK_PACK_START);

  // Update the accelerator group.
  if (menu_bar_)
    gtk_window_remove_accel_group(window_,
                                  menu_bar_->accel_manager()->accel_group());
  if (menu_bar)
    gtk_window_add_accel_group(window_,
                               menu_bar->accel_manager()->accel_group());

  ForceSizeAllocation(window_, GTK_WIDGET(vbox));
}

void Window::SetContentBounds(const RectF& bounds) {
  RectF window_bounds(bounds);
  GdkWindow* gdkwindow = gtk_widget_get_window(GTK_WIDGET(window_));
  if (gdkwindow) {
    // Get frame size.
    GdkRectangle rect;
    gdk_window_get_frame_extents(gdkwindow, &rect);
    // The position of window without frame, which includes menubar.
    int x, y;
    gdk_window_get_position(gdkwindow, &x, &y);
    window_bounds.Offset(rect.x - x, rect.y - y);
  }
  // The position of menubar.
  GdkRectangle alloc;
  gtk_widget_get_allocation(content_view_->GetNative(), &alloc);
  window_bounds.Inset(-alloc.x, -alloc.y, 0, 0);

  ResizeWindow(this, IsResizable(),
               window_bounds.width(), window_bounds.height());
  gtk_window_move(window_, window_bounds.x(), window_bounds.y());
}

RectF Window::GetContentBounds() const {
  GdkWindow* gdkwindow = gtk_widget_get_window(GTK_WIDGET(window_));
  // The position of window without frame, which includes menubar.
  int x, y;
  if (gdkwindow)
    gdk_window_get_position(gdkwindow, &x, &y);
  else
    gtk_window_get_position(window_, &x, &y);
  // The relative position of content view.
  GdkRectangle alloc;
  gtk_widget_get_allocation(content_view_->GetNative(), &alloc);
  return RectF(x + alloc.x, y + alloc.y, alloc.width, alloc.height);
}

void Window::SetBounds(const RectF& bounds) {
  SizeF window_size(bounds.size());
  GdkWindow* gdkwindow = gtk_widget_get_window(GTK_WIDGET(window_));
  if (gdkwindow) {
    // Get frame size.
    GdkRectangle rect;
    gdk_window_get_frame_extents(gdkwindow, &rect);
    // Subtract gdk window size to get frame insets.
    window_size.Enlarge(-(rect.width - gdk_window_get_width(gdkwindow)),
                        -(rect.height - gdk_window_get_height(gdkwindow)));
  }

  ResizeWindow(this, IsResizable(), window_size.width(), window_size.height());
  gtk_window_move(window_, bounds.x(), bounds.y());
}

RectF Window::GetBounds() const {
  GdkWindow* gdkwindow = gtk_widget_get_window(GTK_WIDGET(window_));
  if (gdkwindow && HasFrame()) {
    // For frameless window using CSD, the window size includes the shadows
    // so the frame extents can not be used.
    GdkRectangle rect;
    gdk_window_get_frame_extents(gdkwindow, &rect);
    return RectF(Rect(rect));
  } else {
    int x, y, width, height;
    gtk_window_get_position(window_, &x, &y);
    gtk_window_get_size(window_, &width, &height);
    return RectF(x, y, width, height);
  }
}

void Window::SetVisible(bool visible) {
  gtk_widget_set_visible(GTK_WIDGET(window_), visible);
}

bool Window::IsVisible() const {
  return gtk_widget_get_visible(GTK_WIDGET(window_));
}

void Window::SetResizable(bool resizable) {
  if (resizable == IsResizable())
    return;

  // Current size of content view (gtk_window_get_size is not reliable when
  // window is not realized).
  GdkRectangle alloc;
  GtkWidget* vbox = gtk_bin_get_child(GTK_BIN(window_));
  gtk_widget_get_allocation(vbox, &alloc);

  // Prevent window from changing size when calling gtk_window_set_resizable.
  if (resizable) {
    // Clear the size requests for resizable window, otherwise window would have
    // a minimum size.
    gtk_widget_set_size_request(GTK_WIDGET(window_), -1, -1);
    gtk_widget_set_size_request(gtk_bin_get_child(GTK_BIN(window_)), -1, -1);
    // Window would be resized to default size for resizable window.
    gtk_window_set_default_size(window_, alloc.width, alloc.height);
  } else {
    // Set size requests for unresizable window, otherwise window would be
    // resize to whatever current size request is.
    ResizeWindow(this, resizable, alloc.width, alloc.height);
  }

  gtk_window_set_resizable(window_, resizable);
}

bool Window::IsResizable() const {
  return gtk_window_get_resizable(window_);
}

void Window::SetMaximizable(bool yes) {
  // In theory it is possible to set _NET_WM_ALLOWED_ACTIONS WM hint to
  // implement this, but as I have tried non major desktop environment supports
  // this hint, so on Linux this is simply impossible to implement.
}

bool Window::IsMaximizable() const {
  return IsResizable();
}

void Window::SetBackgroundColor(Color color) {
  GdkRGBA gcolor = color.ToGdkRGBA();
  gtk_widget_override_background_color(GTK_WIDGET(window_),
                                       GTK_STATE_FLAG_NORMAL, &gcolor);
}

}  // namespace nu
