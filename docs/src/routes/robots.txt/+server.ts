import { siteUrl } from '#lib/seo.ts';

export const prerender = true;

export function GET() {
    return new Response(`User-agent: *\nAllow: /\nDisallow: /search.json\nDisallow: /admin\nDisallow: /analytics/\n\nSitemap: ${siteUrl('/sitemap.xml')}\n`, {
        headers: { 'content-type': 'text/plain; charset=utf-8' },
    });
}
