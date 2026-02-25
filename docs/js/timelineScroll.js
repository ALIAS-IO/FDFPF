document.addEventListener("DOMContentLoaded", function () {
    const items = document.querySelectorAll(".timeline-item");

    const observerOptions = {
        root: null,
        rootMargin: "0px",
        threshold: 0.15
    };

    const observerCallback = (entries, observer) => {
        // Filter only items that are intersecting
        const visibleEntries = entries.filter(entry => entry.isIntersecting);

        visibleEntries.forEach((entry, index) => {
            // Add stagger: 50ms per item in this batch
            setTimeout(() => {
                entry.target.classList.add("show");
                observer.unobserve(entry.target); // stop observing after animation
            }, index * 100);
        });
    };

    const observer = new IntersectionObserver(observerCallback, observerOptions);

    items.forEach(item => observer.observe(item));
});