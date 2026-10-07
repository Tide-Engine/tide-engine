// Social cards are built with the docs, with the same type and amber as the
// site. SVG makes their wave field reproducible without image services.
import path from 'node:path';
import { sidebar } from '#lib/nav.ts';
import { page } from '#lib/server/content.ts';

export const socialWidth = 1200;
export const socialHeight = 630;

interface SocialCard {
    title: string;
    section: string;
    kind: 'home' | 'docs' | 'access';
}

const documents = new Map(sidebar.flatMap((group) => group.items.map((item) => [item.link.slice(1), group.text] as const)));

/** Only known pages get cards; the PNG suffix is part of the route. */
export function socialCard(slug: string): SocialCard | undefined {
    if (slug === 'home.png') return { title: 'Tide Engine', section: 'A NETWORKING-FIRST GAME ENGINE', kind: 'home' };
    if (slug === 'request-access.png') return { title: 'Build your next\ngame with Tide.', section: 'EARLY ACCESS', kind: 'access' };
    if (!slug.endsWith('.png')) return;
    const document = slug.slice(0, -4);
    const section = documents.get(document);
    if (!section) return;
    return { title: page(document).title, section: section.toUpperCase(), kind: 'docs' };
}

export const socialEntries = ['home.png', 'request-access.png', ...[...documents.keys()].map((slug) => `${slug}.png`)];

function xml(value: string): string {
    return value.replace(/[&<>"']/g, (character) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' })[character]!);
}

/** A swept surface, with the same three waves as Tide's mark. */
function waves(): string {
    const contours: string[] = [];
    for (let line = 0; line < 34; line++) {
        const points: string[] = [];
        for (let step = 0; step <= 72; step++) {
            const x = 560 + step * 11;
            const offset = line * 5.9;
            const y = 290 + offset + Math.sin((x - 470) / 144 + line * 0.045) * (87 + line * 0.4) - (x - 690) * 0.23;
            points.push(`${step ? 'L' : 'M'}${x.toFixed(2)} ${y.toFixed(2)}`);
        }
        contours.push(`<path d="${points.join(' ')}" stroke="url(#wave)" stroke-width="${line % 11 === 0 ? 2.1 : 0.9}" opacity="${line % 11 === 0 ? 0.9 : 0.35}"/>`);
    }
    return contours.join('');
}

function logo(x: number, y: number, size: number): string {
    return `<g transform="translate(${x} ${y}) scale(${size / 64})" fill="none" stroke="#f59e0b" stroke-width="6" stroke-linecap="round">
        <path d="M6 18q6.5-8 13 0t13 0 13 0 13 0"/>
        <path d="M6 32q6.5-8 13 0t13 0 13 0 13 0"/>
        <path d="M6 46q6.5-8 13 0t13 0 13 0 13 0"/>
    </g>`;
}

export async function renderSocialImage(card: SocialCard): Promise<Uint8Array<ArrayBuffer>> {
    // The native renderer is needed only by the build and development server.
    // Keeping this import here leaves the deployed server dependency-free.
    const { Resvg } = await import('@resvg/resvg-js');
    const font = {
        loadSystemFonts: false,
        fontFiles: ['Inter-Regular.ttf', 'Inter-Bold.ttf'].map((file) => path.resolve('src/lib/server/social-fonts', file)),
        defaultFontFamily: 'Inter',
    };
    const measure = (text: string, size: number) => new Resvg(`<svg xmlns="http://www.w3.org/2000/svg" width="2000" height="200"><text x="0" y="150" font-family="Inter" font-size="${size}" font-weight="700" letter-spacing="-2">${xml(text)}</text></svg>`, { font }).innerBBox()?.width ?? 0;

    // Find the most balanced word break using the font's actual glyph widths.
    // New titles fit without relying on the machine's fonts or character counts.
    let titleSize = card.kind === 'home' ? 116 : 82;
    let lines = card.title.split('\n');
    if (lines.length === 1 && card.kind !== 'home') {
        const words = card.title.split(' ');
        for (; titleSize >= 56; titleSize -= 2) {
            if (measure(card.title, titleSize) <= 815) {
                lines = [card.title];
                break;
            }
            const breaks = words.slice(1).map((_, index) => {
                const pair = [words.slice(0, index + 1).join(' '), words.slice(index + 1).join(' ')];
                const widths = pair.map((line) => measure(line, titleSize));
                return { pair, width: Math.max(...widths), balance: Math.abs(widths[0] - widths[1]) };
            }).filter((split) => split.width <= 815).sort((a, b) => a.balance - b.balance);
            if (breaks.length) {
                lines = breaks[0].pair;
                break;
            }
        }
    }

    const hero = card.kind === 'home';
    const titleY = hero ? 288 : lines.length > 1 ? 298 : 344;
    const title = hero
        ? `<text x="68" y="286" font-size="116" font-weight="700" letter-spacing="-6">Tide</text><text x="68" y="410" fill="#fbbf24" font-size="116" font-weight="700" letter-spacing="-6">Engine</text>`
        : lines.map((line, index) => `<text x="68" y="${titleY + index * (titleSize + 13)}" font-size="${titleSize}" font-weight="700" letter-spacing="-2">${xml(line)}</text>`).join('');
    const subtitle = hero ? 'One simulation. Every player.' : card.kind === 'access' ? 'Request early access.' : 'Tide Engine documentation';
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${socialWidth}" height="${socialHeight}" viewBox="0 0 ${socialWidth} ${socialHeight}">
        <defs>
            <linearGradient id="background" x2="1" y2="0.7"><stop stop-color="#111216"/><stop offset="0.63" stop-color="#16171b"/><stop offset="1" stop-color="#252117"/></linearGradient>
            <radialGradient id="glow"><stop stop-color="#dc880f" stop-opacity="0.2"/><stop offset="1" stop-color="#dc880f" stop-opacity="0"/></radialGradient>
            <linearGradient id="wave"><stop stop-color="#f59e0b" stop-opacity="0"/><stop offset="0.38" stop-color="#f59e0b" stop-opacity="0.42"/><stop offset="0.72" stop-color="#ffbf3c"/><stop offset="1" stop-color="#fbcc75" stop-opacity="0.65"/></linearGradient>
            <pattern id="grid" width="46" height="46" patternUnits="userSpaceOnUse"><path d="M46 0H0V46" fill="none" stroke="#b6a47a" stroke-opacity="0.065" stroke-width="1"/></pattern>
            <linearGradient id="shade"><stop stop-color="#111216"/><stop offset="0.54" stop-color="#111216" stop-opacity="0.88"/><stop offset="0.84" stop-color="#111216" stop-opacity="0"/></linearGradient>
        </defs>
        <rect width="1200" height="630" fill="url(#background)"/>
        <ellipse cx="1060" cy="278" rx="620" ry="510" fill="url(#glow)"/>
        <rect width="1200" height="630" fill="url(#grid)"/>
        <g fill="none">${waves()}</g>
        <rect width="1000" height="630" fill="url(#shade)"/>
        <path d="M1010 88h104v104M1114 440v104h-104" fill="none" stroke="#bba06a" stroke-opacity="0.22"/>
        <circle cx="1114" cy="88" r="3" fill="#fbbf24"/>
        <g fill="#f1f1eb" font-family="Inter">
            ${logo(65, 53, 48)}
            <text x="129" y="87" font-size="29" font-weight="700" letter-spacing="-0.7">Tide Engine</text>
            <text x="68" y="174" fill="#fbbf24" font-size="18" font-weight="700" letter-spacing="2.6">${xml(card.section)}</text>
            ${title}
            <text x="70" y="${hero ? 471 : 461}" fill="#b5b5b9" font-size="24" letter-spacing="-0.35">${xml(subtitle)}</text>
            <text x="68" y="574" font-size="19" fill="#c5c3bc" letter-spacing="0.2">tide-engine.dev</text>
            <text x="1130" y="574" text-anchor="end" font-size="13" fill="#a39374" font-weight="700" letter-spacing="2.3">${hero ? 'BUILT FOR MULTIPLAYER' : card.kind === 'access' ? 'CREATE WITH TIDE' : 'LEARN. BUILD. PLAY.'}</text>
        </g>
        <path d="M68 529H1132" stroke="#b9ab8b" stroke-opacity="0.16"/>
    </svg>`;
    return new Uint8Array(new Resvg(svg, { font }).render().asPng());
}
