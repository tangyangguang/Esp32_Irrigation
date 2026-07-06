const pageMeta = {
  overview: { title: "总览", crumb: "系统 / 总览", actions: [] },
  run: { title: "运行", crumb: "手动浇水 / 运行", actions: [] },
  plans: { title: "计划", crumb: "自动浇水 / 计划", actions: [["新建计划", "./plan-form.html"]] },
  sources: { title: "水源管理", crumb: "对象配置 / 水源管理", actions: [["新建水源", "./source-form.html"]] },
  history: { title: "浇水历史", crumb: "记录 / 浇水历史", actions: [] },
  settings: { title: "系统参数", crumb: "全局配置 / 系统参数", actions: [["保存参数", ""]] }
};

const pageTitleEl = document.getElementById("pageTitle");
const crumbEl = document.getElementById("crumb");
const topActionsEl = document.getElementById("topActions");

function renderActions(actions) {
  topActionsEl.innerHTML = "";
  actions.forEach(([label, href]) => {
    if (href) {
      const a = document.createElement("a");
      a.className = "btn";
      a.textContent = label;
      a.href = href;
      topActionsEl.appendChild(a);
    } else {
      const b = document.createElement("button");
      b.className = "btn";
      b.textContent = label;
      topActionsEl.appendChild(b);
    }
  });
}

function setPage(id) {
  const meta = pageMeta[id];
  document.querySelectorAll(".page").forEach(page => page.classList.toggle("active", page.id === id));
  document.querySelectorAll(".nav [data-page], .mobile-nav [data-page]").forEach(button => {
    button.classList.toggle("active", button.dataset.page === id);
  });
  pageTitleEl.textContent = meta.title;
  crumbEl.textContent = meta.crumb;
  renderActions(meta.actions);
  window.scrollTo(0, 0);
}

document.addEventListener("click", event => {
  const nav = event.target.closest("[data-page]");
  if (nav) setPage(nav.dataset.page);

  const seg = event.target.closest(".segmented button");
  if (seg) {
    seg.parentElement.querySelectorAll("button").forEach(button => button.classList.toggle("active", button === seg));
  }
});

const initialPage = window.location.hash.slice(1);
if (pageMeta[initialPage]) setPage(initialPage);
