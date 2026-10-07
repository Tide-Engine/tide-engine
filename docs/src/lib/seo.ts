export const SITE_ORIGIN = 'https://tide-engine.dev';
export const SITE_NAME = 'Tide Engine';

/** Public URLs are independent of a preview host, query string or fragment. */
export const siteUrl = (path: string) => `${SITE_ORIGIN}${path}`;

export const socialImage = (path: string) => siteUrl(`/social/${path === '/' ? 'home' : path.slice(1)}.png`);

/** Escape HTML delimiters before putting JSON in a script element. */
export const jsonLd = (value: unknown) => JSON.stringify(value).replace(/</g, '\\u003c');
