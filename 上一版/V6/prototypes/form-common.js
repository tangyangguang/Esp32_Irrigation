document.addEventListener("click", event => {
  const seg = event.target.closest(".segmented button");
  if (seg) {
    seg.parentElement.querySelectorAll("button").forEach(button => button.classList.toggle("active", button === seg));
    return;
  }
  const toggle = event.target.closest(".toggle");
  if (toggle) {
    toggle.classList.toggle("on");
    return;
  }
  const day = event.target.closest(".day-picker button");
  if (day) {
    day.classList.toggle("active");
  }
});
