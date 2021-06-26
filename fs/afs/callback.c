/*
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Woodhouse <dwmw2@cambridge.redhat.com>
 *          David Howells <dhowells@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include "server.h"
#include "vnode.h"
#include "internal.h"

/*****************************************************************************/
/*
 * allow the fileserver to request callback state (re-)initialisation
 */
int SRXAFSCM_InitCallBackState(afs_server_t *server)
{
	struct list_head callbacks;

	_enter("%p",server);

	INIT_LIST_HEAD(&callbacks);

	/* transfer the callback list from the server to a temp holding area */
	spin_lock(&server->cb_lock);

	list_add(&callbacks,&server->cb_promises);
	list_del_init(&server->cb_promises);

	/* munch our way through the list, grabbing the inode, dropping all the locks and regetting
	 * them in the right order
	 */
	while (!list_empty(&callbacks)) {
		struct inode *inode;
		afs_vnode_t *vnode;

		vnode = list_entry(callbacks.next,afs_vnode_t,cb_link);
		list_del_init(&vnode->cb_link);

		/* try and grab the inode - may fail */
		inode = igrab(AFS_VNODE_TO_I(vnode));
		if (inode) {
			int release = 0;

			spin_unlock(&server->cb_lock);
			spin_lock(&vnode->lock);

			if (cmpxchg(&vnode->cb_server,server,NULL)==server) {
				afs_kafstimod_del_timer(&vnode->cb_timeout);
				spin_lock(&afs_cb_hash_lock);
				list_del_init(&vnode->cb_hash_link);
				spin_unlock(&afs_cb_hash_lock);
				release = 1;
			}

			spin_unlock(&vnode->lock);

			iput(inode);
			if (release) afs_put_server(server);

			spin_lock(&server->cb_lock);
		}
	}

	spin_unlock(&server->cb_lock);

	_leave(" = 0");
	return 0;
} /* end SRXAFSCM_InitCallBackState() */

/*****************************************************************************/
/*
 * allow the fileserver to break callback promises
 */
int SRXAFSCM_CallBack(afs_server_t *server, size_t count, afs_callback_t callbacks[])
{
	struct list_head *_p;

	_enter("%p,%u,",server,count);

	for (; count>0; callbacks++, count--) {
		struct inode *inode = NULL;
		afs_vnode_t *vnode = NULL;
		int valid = 0;

		_debug("- Fid { vl=%08x n=%u u=%u }  CB { v=%u x=%u t=%u }",
		       callbacks->fid.vid,
		       callbacks->fid.vnode,
		       callbacks->fid.unique,
		       callbacks->version,
		       callbacks->expiry,
		       callbacks->type
		       );

		/* find the inode for this fid */
		spin_lock(&afs_cb_hash_lock);

		list_for_each(_p,&afs_cb_hash(server,&callbacks->fid)) {
			vnode = list_entry(_p,afs_vnode_t,cb_hash_link);

			if (memcmp(&vnode->fid,&callbacks->fid,sizeof(afs_fid_t))!=0)
				continue;

			/* right vnode, but is it same server? */
			if (vnode->cb_server!=server)
				break; /* no */

			/* try and nail the inode down */
			inode = igrab(AFS_VNODE_TO_I(vnode));
			break;
		}

		spin_unlock(&afs_cb_hash_lock);

		if (inode) {
			/* we've found the record for this vnode */
			spin_lock(&vnode->lock);
			if (cmpxchg(&vnode->cb_server,server,NULL)==server) {
				/* the callback _is_ on the calling server */
				valid = 1;

				afs_kafstimod_del_timer(&vnode->cb_timeout);
				vnode->flags |= AFS_VNODE_CHANGED;

				spin_lock(&server->cb_lock);
				list_del_init(&vnode->cb_link);
				spin_unlock(&server->cb_lock);

				spin_lock(&afs_cb_hash_lock);
				list_del_init(&vnode->cb_hash_link);
				spin_unlock(&afs_cb_hash_lock);
			}
			spin_unlock(&vnode->lock);

			if (valid) {
				invalidate_inode_pages(inode);
				afs_put_server(server);
			}
			iput(inode);
		}
	}

	_leave(" = 0");
	return 0;
} /* end SRXAFSCM_CallBack() */

/*****************************************************************************/
/*
 * allow the fileserver to see if the cache manager is still alive
 */
int SRXAFSCM_Probe(afs_server_t *server)
{
	_debug("SRXAFSCM_Probe(%p)\n",server);
	return 0;
} /* end SRXAFSCM_Probe() */