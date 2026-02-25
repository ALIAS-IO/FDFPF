function openModalFromLink(targetModalId, currentLink) {
    const currentModal = currentLink.closest('.modal.show');
    if (currentModal) {
        const bsModal = bootstrap.Modal.getInstance(currentModal);
        bsModal.hide();
    }

    setTimeout(() => {
        const modal = new bootstrap.Modal(document.getElementById(targetModalId));
        modal.show();
    }, 300);
}