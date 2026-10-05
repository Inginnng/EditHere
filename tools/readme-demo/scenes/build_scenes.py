"""Renders the material the README recordings are made from: a desktop showing a
mock dashboard in each interface language, a long article for scrolling capture,
and a short clip for video annotation. Everything is synthetic so no real product
or person appears in the published GIFs.

    python tools/readme-demo/scenes/build_scenes.py
"""
import pathlib
import subprocess

import imageio_ffmpeg
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = HERE.parent / "material"
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
SCALE = 2  # The recorder renders on a 2x stage so the GIFs stay crisp when scaled down.

TEXT = {
    "zh": dict(nav1="概览", nav2="订单", nav3="商品", nav4="客户", nav5="设置",
               plan="专业版剩余 12 天<br>升级以解锁更多报表", upgrade="立即升级", title="经营概览",
               subtitle="2026 年 9 月 · 实时更新", search="搜索订单、客户…", export="导出报表",
               k1="本月营收", k2="订单数", k3="转化率", k4="客户评分", chart="月度营收",
               range="近 8 个月", months="".join(f"<span>{m}月</span>" for m in range(2, 10)),
               mix="渠道构成", week="本周", c1="直营", c2="小程序", c3="电商", c4="其他",
               orders="最新订单", all="查看全部", t1="订单号", t2="商品", t3="金额", t4="状态",
               p1="降噪耳机 Pro", p2="便携咖啡杯", paid="已支付", pending="待发货",
               doc="版本更新日志", section="第 {n} 节",
               p_a="EditHere 让界面反馈落在画面上：圈出位置、写下意见、拖动组件，再把一份结构化反馈交给 AI。",
               p_b="长截图会在你滚动时持续拼接，固定的顶栏只保留一次，预览随内容向下延伸。"),
    "en": dict(nav1="Overview", nav2="Orders", nav3="Products", nav4="Customers", nav5="Settings",
               plan="12 days left on Pro<br>Upgrade for more reports", upgrade="Upgrade now",
               title="Business overview", subtitle="September 2026 · Live", search="Search orders, customers…",
               export="Export report", k1="Revenue this month", k2="Orders", k3="Conversion",
               k4="Customer rating", chart="Monthly revenue", range="Last 8 months",
               months="".join(f"<span>{m}</span>" for m in "Feb Mar Apr May Jun Jul Aug Sep".split()),
               mix="Channel mix", week="This week", c1="Direct", c2="Mini app", c3="Online", c4="Other",
               orders="Latest orders", all="View all", t1="Order", t2="Product", t3="Amount", t4="Status",
               p1="Noise-cancelling Pro", p2="Travel coffee cup", paid="Paid", pending="To ship",
               doc="Release notes", section="Section {n}",
               p_a="EditHere puts interface feedback on the picture itself: mark the spot, write the note, "
                   "drag the part, then hand one structured file to your AI.",
               p_b="Scrolling capture keeps stitching while you scroll; a sticky header is kept once and the "
                   "preview grows with the page."),
}

DASHBOARD = """<div class="dash"><nav><div class="logo"><i></i>Lumen</div>
<a class="on"><b></b>{nav1}</a><a><b></b>{nav2}</a><a><b></b>{nav3}</a><a><b></b>{nav4}</a><a><b></b>{nav5}</a>
<div class="plan">{plan}<button>{upgrade}</button></div></nav>
<main><header><div><h1>{title}</h1><p>{subtitle}</p></div><div class="tools"><div class="search">{search}</div><div class="btn">{export}</div><div class="ava"></div></div></header>
<div class="kpis"><div class="kpi"><span>{k1}</span><strong>¥ 842,310</strong><b class="up">▲ 12.4%</b></div><div class="kpi"><span>{k2}</span><strong>18,204</strong><b class="up">▲ 6.1%</b></div><div class="kpi"><span>{k3}</span><strong>3.42%</strong><b class="down">▼ 0.8%</b></div><div class="kpi"><span>{k4}</span><strong>4.8 / 5</strong><b class="up">▲ 0.2</b></div></div>
<div class="row"><div class="card"><h3>{chart}<em>{range}</em></h3><div class="chart"><div style="height:42%"></div><div style="height:55%"></div><div style="height:48%"></div><div style="height:66%"></div><div style="height:60%"></div><div class="hi" style="height:88%"></div><div style="height:72%"></div><div style="height:79%"></div></div><div class="months">{months}</div></div>
<div class="card"><h3>{mix}<em>{week}</em></h3><div class="donut"></div><div class="legend"><span style="--c:#ff6a6a">{c1} 42%</span><span style="--c:#ffb35c">{c2} 26%</span><span style="--c:#5b8def">{c3} 20%</span><span style="--c:#cfd8ea">{c4} 12%</span></div></div></div>
<div class="card"><h3>{orders}<em>{all}</em></h3><table><tr><th>{t1}</th><th>{t2}</th><th>{t3}</th><th>{t4}</th></tr>
<tr><td>#10482</td><td>{p1}</td><td>¥ 1,299</td><td><span class="tag ok">{paid}</span></td></tr>
<tr><td>#10481</td><td>{p2}</td><td>¥ 459</td><td><span class="tag wait">{pending}</span></td></tr></table></div>
</main></div>"""

ARTICLE_STYLE = """<style>
.doc{width:1200px;font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#fff;color:#1d2433}
.doc .top{height:54px;background:#141a2b;color:#fff;display:flex;align-items:center;padding:0 28px;font-weight:700;gap:10px}
.doc .top i{width:22px;height:22px;border-radius:7px;background:linear-gradient(135deg,#ff8a5b,#ff4f81)}
.doc h1{font-size:28px;padding:28px 200px 6px}.doc .lead{padding:0 200px 18px;color:#7c8599;font-size:13px}
.doc section{display:grid;grid-template-columns:180px 1fr;gap:24px;padding:20px 200px;border-top:1px solid #eef1f5}
.doc .pic{height:108px;border-radius:14px;display:grid;place-items:center;color:#fff;font-size:30px;font-weight:700}
.doc h2{font-size:17px;margin-bottom:7px}.doc p{font-size:13.5px;line-height:1.7;color:#4b5468;margin-bottom:5px}
</style>"""


def shoot(html: pathlib.Path, png: pathlib.Path, w: int, h: int):
    subprocess.run([EDGE, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                    f"--force-device-scale-factor={SCALE}", f"--window-size={w},{h}",
                    f"--screenshot={png}", html.as_uri()], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def article(t: dict) -> str:
    colors = ["#ff8a5b", "#5b8def", "#14a06f", "#ffb35c", "#a46bf5", "#ff4f81"]
    blocks = []
    for i in range(16):
        a, b = (t["p_a"], t["p_b"]) if i % 2 == 0 else (t["p_b"], t["p_a"])
        blocks.append(
            f'<section><div class="pic" style="background:linear-gradient(135deg,{colors[i % 6]},'
            f'{colors[(i + 2) % 6]})">{i + 1:02d}</div><div><h2>{t["section"].format(n=i + 1)}'
            f' · {t["title"]}</h2><p>{a}</p><p>{b}</p></div></section>')
    return (f'{ARTICLE_STYLE}<div class="doc"><div class="top"><i></i>Lumen Docs</div><h1>{t["doc"]}</h1>'
            f'<p class="lead">{t["subtitle"]}</p>{"".join(blocks)}</div>')


def video():
    """An eight-second pan over the game scene that changes halfway, so pausing at
    two moments gives two clearly different frames to annotate."""
    src = Image.open(ROOT / "assets/readme/game-before.png").convert("RGB")
    after = Image.open(ROOT / "assets/readme/game-after.png").convert("RGB")
    W, H, FPS, SECS = 1280, 720, 30, 8
    proc = subprocess.Popen([imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-v", "error", "-f", "rawvideo",
                             "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
                             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18",
                             str(OUT / "gameplay.mp4")], stdin=subprocess.PIPE)
    frames = FPS * SECS
    for i in range(frames):
        t = i / (frames - 1)
        img = src if t < .55 else Image.blend(src, after, min(1, (t - .55) / .2))
        zoom = 1.0 + .06 * t
        cw, ch = src.width / zoom, src.height / zoom
        x0, y0 = (src.width - cw) * (.2 + .6 * t), (src.height - ch) * .5
        frame = img.crop((int(x0), int(y0), int(x0 + cw), int(y0 + ch))).resize((W, H), Image.LANCZOS)
        proc.stdin.write(frame.tobytes())
    proc.stdin.close()
    proc.wait()


# What the dashboard looks like after the AI has acted on the feedback given in the
# agent demo: the export button takes the brand colour, the revenue card is
# emphasised and the upgrade card leaves the sidebar.
AFTER = (".btn{background:#ff5f6d !important}.plan{display:none !important}"
         ".kpi:first-child{border:2px solid #ff8a5b}")


def main():
    OUT.mkdir(exist_ok=True)
    desktop = (HERE / "desktop.html").read_text(encoding="utf-8")
    for lang, words in TEXT.items():
        common = {"tabtitle": "Lumen · " + words["title"], "url": "https://lumen.example/" +
                  ("dashboard" if lang == "en" else "dashboard?lang=zh")}
        for name, content, size, extra in (("desktop", DASHBOARD.format(**words), (1200, 750), ""),
                                           ("desktop-after", DASHBOARD.format(**words), (1200, 750), AFTER),
                                           ("desktop-docs", article(words), (1200, 750), ""),
                                           ("article", article(words), (1200, 2600), "")):
            html = desktop if name.startswith("desktop") else ("<!doctype html><meta charset='utf-8'>"
                                                                "<body style='margin:0'>{{content}}</body>")
            html = html.replace("{{content}}", content).replace("{{extra}}", extra)
            values = dict(common)
            if name == "desktop-docs":
                values = {"tabtitle": "Lumen Docs · " + words["doc"], "url": "https://lumen.example/docs/changelog"}
            for key, value in values.items():
                html = html.replace("{{" + key + "}}", value)
            page = HERE / f"{name}-{lang}.html"
            page.write_text(html, encoding="utf-8")
            shoot(page, OUT / f"{name}-{lang}.png", *size)
    video()
    print("material ready:", OUT)


if __name__ == "__main__":
    main()
