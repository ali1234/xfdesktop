/*
 *  xfdesktop - xfce4's desktop manager
 *
 *  Copyright (c) 2006 Brian Tarricone, <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>

#include "xfdesktop-icon-view-manager.h"
#include "xfdesktop-icon-view.h"

GType
xfdesktop_icon_view_manager_get_type()
{
    static GType manager_type = 0;
    
    if(!manager_type) {
        static const GTypeInfo manager_info = {
            sizeof(XfdesktopIconViewManagerIface),
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            0,
            0,
            NULL,
        };
        
        manager_type = g_type_register_static(G_TYPE_INTERFACE,
                                            "XfdesktopIconViewManager",
                                            &manager_info, 0);
        g_type_interface_add_prerequisite(manager_type, G_TYPE_OBJECT);
    }
    
    return manager_type;
}

gboolean
xfdesktop_icon_view_manager_init(XfdesktopIconViewManager *manager,
                                 XfdesktopIconView *icon_view)
{
    XfdesktopIconViewManagerIface *iface;
    
    g_return_val_if_fail(XFDESKTOP_IS_ICON_VIEW_MANAGER(manager)
                         && XFDESKTOP_IS_ICON_VIEW(icon_view), FALSE);
    
    iface = XFDESKTOP_ICON_VIEW_MANAGER_GET_IFACE(manager);
    g_return_val_if_fail(iface->manager_init, FALSE);
    
    return iface->manager_init(manager, icon_view);
}

void
xfdesktop_icon_view_manager_fini(XfdesktopIconViewManager *manager,
                                 XfdesktopIconView *icon_view)
{
    XfdesktopIconViewManagerIface *iface;
    
    g_return_if_fail(XFDESKTOP_IS_ICON_VIEW_MANAGER(manager)
                     && XFDESKTOP_IS_ICON_VIEW(icon_view));
    
    iface = XFDESKTOP_ICON_VIEW_MANAGER_GET_IFACE(manager);
    g_return_if_fail(iface->manager_fini);
    
    iface->manager_fini(manager, icon_view);
}
