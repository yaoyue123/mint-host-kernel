#ifndef _UAPI__KVM_PAGE_TRACK_H
#define _UAPI__KVM_PAGE_TRACK_H

enum kvm_page_track_mode {
	KVM_PAGE_TRACK_WRITE,
	KVM_PAGE_TRACK_ACCESS,
	KVM_PAGE_TRACK_RESET_ACCESSED, //TODO: hacky, as this is not really for page tracking
	KVM_PAGE_TRACK_EXEC,
	KVM_PAGE_TRACK_RESET_EXEC,
	KVM_PAGE_TRACK_MAX,
};

#endif