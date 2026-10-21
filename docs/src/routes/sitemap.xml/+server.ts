import { pages } from '#lib/nav.ts';
import { siteUrl } from '#lib/seo.ts';

export const prerender = true;

export function GET() {
    const paths = ['/', ...pages.map((page) => page.link), '/request-access', '/privacy'];
    const urls = paths.map((path) => `<url><loc>${siteUrl(path)}</loc></url>`).join('\n');
    return new Response(`<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${urls}\n</urlset>\n`, {
        headers: { 'content-type': 'application/xml; charset=utf-8' },
    });
}
