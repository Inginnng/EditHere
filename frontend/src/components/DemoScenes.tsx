import { useId } from 'react';

export function HomeSceneSvg({ stretch = false }: { stretch?: boolean }) {
  const uid = useId().replace(/:/g, '');
  const shadowId = `eh-chair-shadow-${uid}`;
  const clipId = `eh-hero-clip-${uid}`;

  return (
    <svg
      className="artboard-svg"
      viewBox="0 0 800 480"
      preserveAspectRatio={stretch ? 'none' : 'xMidYMid meet'}
      role="img"
      aria-label="Morrow 首页首屏"
    >
      <defs>
        <filter id={shadowId} x="-60%" y="-60%" width="220%" height="220%">
          <feGaussianBlur stdDeviation="9" />
        </filter>
        <clipPath id={clipId}>
          <rect x="405" y="62" width="395" height="355" />
        </clipPath>
      </defs>
      <rect width="800" height="480" fill="#fffefa" />
      <text x="38" y="36" fill="#263a2c" fontFamily="Georgia,serif" fontSize="25" fontWeight="600" letterSpacing="-1.4">
        morrow<tspan fill="#b06344">.</tspan>
      </text>
      <g fill="#647064" fontFamily="Arial,sans-serif" fontSize="9">
        <text x="324" y="32">COLLECTION</text>
        <text x="421" y="32">OUR STORY</text>
        <text x="513" y="32">JOURNAL</text>
        <text x="715" y="32">BAG (0)</text>
      </g>
      <path d="M0 62H800" stroke="#e9e9de" />
      <rect y="63" width="800" height="354" fill="#f0f1e8" />
      <text x="43" y="113" fill="#758071" fontFamily="Arial,sans-serif" fontSize="8" letterSpacing="2">
        CONSIDERED LIVING, EVERY DAY.
      </text>
      <text x="39" y="178" fill="#2c402f" fontFamily="Georgia,serif" fontSize="58" letterSpacing="-2.5">
        A quieter
      </text>
      <text x="39" y="235" fill="#2c402f" fontFamily="Georgia,serif" fontSize="58" letterSpacing="-2.5">
        kind of home.
      </text>
      <text x="43" y="272" fill="#7d8476" fontFamily="Arial,sans-serif" fontSize="10">
        Thoughtfully made objects. A little closer to nature.
      </text>
      <text x="43" y="289" fill="#7d8476" fontFamily="Arial,sans-serif" fontSize="10">
        Find your everyday, beautifully reimagined.
      </text>
      <rect x="43" y="316" width="134" height="37" rx="3" fill="#b67152" />
      <text x="53" y="339" fill="#fffefa" fontFamily="Arial,sans-serif" fontSize="9" letterSpacing=".6">
        EXPLORE THE EDIT
      </text>
      <path d="m161 330 5 5-5 5m-8-5h13" stroke="#fffefa" fill="none" />
      <g clipPath={`url(#${clipId})`}>
        <path d="M498 63H800V417H498Z" fill="#e5e8dc" />
        <path d="M498 63H525V417H498Z" fill="#daddcf" />
        <circle cx="736" cy="139" r="73" fill="#d4ad81" />
        <path d="M579 348 800 286V417H405Z" fill="#dedecf" />
        <ellipse cx="643" cy="357" rx="105" ry="13" fill="#5e6652" opacity=".14" filter={`url(#${shadowId})`} />
        <path d="M697 252 721 367M572 258 555 366" stroke="#845c3c" strokeWidth="9" strokeLinecap="round" />
        <path d="m573 259 125-8M562 322l146-2" stroke="#9d7955" strokeWidth="6" />
        <path d="M568 253 583 141Q586 124 607 127L680 141Q698 144 697 164L689 251Z" fill="#8d6546" />
        <path d="M578 247 591 149Q592 136 606 139L671 151Q684 154 682 168L675 245Z" fill="#b5a28a" />
        <path d="M555 251Q616 232 697 246L712 274Q645 300 555 279Z" fill="#906d50" />
        <path d="M559 249Q619 230 694 245L700 267Q635 288 558 269Z" fill="#bbaa90" />
        <path d="M765 270q-11-86 8-144M773 201q-26-8-31-34 29 1 31 34m0-28q28-6 25-35-25 7-25 35m-6 71q-27-1-36-29 26-2 36 29" fill="#546449" />
        <path d="m747 280 40 0-7 52h-27Z" fill="#b67655" />
      </g>
      <g fill="#6d786a" fontFamily="Arial,sans-serif" fontSize="8" letterSpacing="1">
        <text x="42" y="452">MADE WITH INTENTION.</text>
        <text x="319" y="452">NATURAL MATERIALS</text>
        <text x="586" y="452">DESIGNED TO STAY.</text>
      </g>
      <path d="m280 442 0 14m268-14v14" stroke="#e1e3d9" />
    </svg>
  );
}

export function InventoryHudSvg({ stretch = false }: { stretch?: boolean }) {
  const slots = Array.from({ length: 18 }, (_, i) => {
    const col = i % 6;
    const row = Math.floor(i / 6);
    const x = 302 + col * 76;
    const y = 128 + row * 70;
    const equipped = i === 0 || i === 3 || i === 7;
    const rare = i === 1 || i === 9;
    return { i, x, y, equipped, rare };
  });

  return (
    <svg
      className="artboard-svg"
      viewBox="0 0 800 480"
      preserveAspectRatio={stretch ? 'none' : 'xMidYMid meet'}
      role="img"
      aria-label="游戏物品栏 HUD"
    >
      <rect width="800" height="480" fill="#131722" />
      <rect x="0" y="0" width="800" height="58" fill="#1b2131" />
      <path d="M0 58H800" stroke="#2d364d" />
      <text x="30" y="36" fill="#f1f5f9" fontFamily="sans-serif" fontSize="16" fontWeight="700" letterSpacing="1">
        INVENTORY · 物品栏
      </text>
      <text x="642" y="35" fill="#fbbf24" fontFamily="monospace" fontSize="13" fontWeight="600">
        GOLD 14,820
      </text>

      {/* Left Character Panel */}
      <rect x="28" y="76" width="234" height="328" rx="8" fill="#1b2232" stroke="#2d374e" />
      <circle cx="145" cy="146" r="36" fill="#27324b" stroke="#60a5fa" strokeWidth="2" />
      <path d="M126 154c4-14 34-14 38 0" stroke="#93c5fd" strokeWidth="3" fill="none" strokeLinecap="round" />
      <circle cx="145" cy="133" r="10" fill="#93c5fd" />
      <text x="145" y="206" textAnchor="middle" fill="#e2e8f0" fontSize="14" fontWeight="600">
        Ranger Lv.42
      </text>
      <g fontFamily="monospace" fontSize="11" fill="#94a3b8">
        <text x="48" y="242">ATK</text><text x="242" y="242" textAnchor="end" fill="#f8fafc">482</text>
        <text x="48" y="268">DEF</text><text x="242" y="268" textAnchor="end" fill="#f8fafc">310</text>
        <text x="48" y="294">CRIT</text><text x="242" y="294" textAnchor="end" fill="#38bdf8">28.5%</text>
        <text x="48" y="320">WEIGHT</text><text x="242" y="320" textAnchor="end" fill="#fbbf24">34 / 50</text>
      </g>
      <rect x="48" y="348" width="194" height="34" rx="6" fill="#243048" />
      <text x="145" y="369" textAnchor="middle" fill="#cbd5e1" fontSize="11">装备对比已启用</text>

      {/* Right Grid */}
      <rect x="280" y="76" width="492" height="268" rx="8" fill="#1a2030" stroke="#2d374e" />
      <text x="302" y="106" fill="#94a3b8" fontFamily="monospace" fontSize="11">
        ALL ITEMS (18 / 24)
      </text>
      {slots.map(({ i, x, y, equipped, rare }) => (
        <g key={i}>
          <rect
            x={x}
            y={y}
            width="64"
            height="58"
            rx="6"
            fill={equipped ? '#233252' : '#1f273b'}
            stroke={equipped ? '#60a5fa' : rare ? '#a855f7' : '#323e59'}
            strokeWidth={equipped ? 2 : 1}
          />
          <circle cx={x + 32} cy={y + 27} r="13" fill={equipped ? '#3b82f6' : rare ? '#9333ea' : '#334155'} opacity="0.7" />
          {equipped && (
            <rect x={x + 4} y={y + 4} width="16" height="12" rx="3" fill="#3b82f6" />
          )}
        </g>
      ))}

      {/* Bottom Action Bar */}
      <rect x="280" y="360" width="492" height="44" rx="8" fill="#1b2131" stroke="#2d374e" />
      <rect x="296" y="370" width="96" height="24" rx="5" fill="#253047" />
      <text x="344" y="386" textAnchor="middle" fill="#cbd5e1" fontSize="11">仅看可装备</text>
      <rect x="556" y="370" width="96" height="24" rx="5" fill="#334155" />
      <text x="604" y="386" textAnchor="middle" fill="#f8fafc" fontSize="11">一键整理</text>
      <rect x="660" y="370" width="96" height="24" rx="5" fill="#2563eb" />
      <text x="708" y="386" textAnchor="middle" fill="#ffffff" fontSize="11" fontWeight="600">批量分解</text>

      <text x="28" y="446" fill="#64748b" fontFamily="monospace" fontSize="11">
        ESC 返回游戏   ·   R 快速整理   ·   E 装备/卸下
      </text>
    </svg>
  );
}

export function SettingsMockSvg({ stretch = false }: { stretch?: boolean }) {
  return (
    <svg
      className="artboard-svg"
      viewBox="0 0 800 480"
      preserveAspectRatio={stretch ? 'none' : 'xMidYMid meet'}
      role="img"
      aria-label="应用设置页示例"
    >
      <rect width="800" height="480" fill="#fafafa" />
      <rect x="20" y="20" width="165" height="440" rx="10" fill="#f3f4f6" stroke="#e5e7eb" />
      <text x="38" y="54" fill="#18181b" fontSize="15" fontWeight="700">偏好设置</text>
      {['通用外观', '快捷键', '本地 OCR', '更新与安全'].map((label, idx) => (
        <g key={label}>
          <rect
            x="30"
            y={76 + idx * 42}
            width="145"
            height="34"
            rx="6"
            fill={idx === 0 ? '#e8f2fc' : 'transparent'}
          />
          <text
            x="44"
            y={97 + idx * 42}
            fill={idx === 0 ? '#0067c0' : '#52525b'}
            fontSize="12"
            fontWeight={idx === 0 ? '600' : '400'}
          >
            {label}
          </text>
        </g>
      ))}
      <text x="212" y="50" fill="#18181b" fontSize="20" fontWeight="600">通用外观</text>
      {[
        { title: '主题配色', desc: '选择浅色、深色或跟随系统主题' },
        { title: '自动附带区域文字', desc: '新建框选批注时在本地执行 OCR 并写入 ocrtext' },
        { title: '高 DPI 原图导出', desc: '保留设备真实像素尺寸，不压缩 PNG' },
        { title: '截图阴影与圆角', desc: '桌面快速截图时保留柔和窗口投影' },
      ].map((row, idx) => (
        <g key={row.title}>
          <rect
            x="212"
            y={70 + idx * 78}
            width="552"
            height="66"
            rx="8"
            fill="#ffffff"
            stroke="#e4e4e7"
          />
          <text x="232" y={98 + idx * 78} fill="#18181b" fontSize="13" fontWeight="600">
            {row.title}
          </text>
          <text x="232" y={118 + idx * 78} fill="#71717a" fontSize="11">
            {row.desc}
          </text>
          <rect x="702" y={92 + idx * 78} width="42" height="22" rx="11" fill={idx !== 3 ? '#0067c0' : '#d4d4d8'} />
          <circle cx={idx !== 3 ? 733 : 713} cy={103 + idx * 78} r="8" fill="#ffffff" />
        </g>
      ))}
      <rect x="212" y="408" width="552" height="44" rx="8" fill="#ffffff" stroke="#e4e4e7" />
      <rect x="614" y="416" width="64" height="28" rx="6" fill="#f4f4f5" stroke="#d4d4d8" />
      <text x="646" y="434" textAnchor="middle" fill="#18181b" fontSize="12">取消</text>
      <rect x="688" y="416" width="64" height="28" rx="6" fill="#0067c0" />
      <text x="720" y="434" textAnchor="middle" fill="#ffffff" fontSize="12">保存</text>
    </svg>
  );
}

export function SceneArtwork({
  sceneId,
  imageUrl,
  stretch = false,
}: {
  sceneId?: 'home' | 'inventory' | 'settings';
  imageUrl?: string;
  stretch?: boolean;
}) {
  if (imageUrl) {
    return (
      <img
        className="artboard-img"
        style={{ objectFit: stretch ? 'fill' : 'contain' }}
        src={imageUrl}
        alt="已导入的页面截图"
      />
    );
  }
  if (sceneId === 'inventory') return <InventoryHudSvg stretch={stretch} />;
  if (sceneId === 'settings') return <SettingsMockSvg stretch={stretch} />;
  return <HomeSceneSvg stretch={stretch} />;
}
