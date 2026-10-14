<script lang="ts">
    import { SITE_NAME, SITE_ORIGIN, siteUrl, socialImage, jsonLd } from '#lib/seo.ts';

    let { title, description, path, documentation = false }: {
        title: string;
        description: string;
        path: string;
        documentation?: boolean;
    } = $props();

    const url = $derived(siteUrl(path));
    const image = $derived(socialImage(path));
    const imageAlt = $derived(`${title} — Tide Engine ${documentation ? 'documentation' : 'social preview'}, with amber waves on a dark background.`);
    const breadcrumbs = $derived([
        { '@type': 'ListItem', position: 1, name: SITE_NAME, item: siteUrl('/') },
        ...(path === '/guide' ? [] : [{ '@type': 'ListItem', position: 2, name: 'Documentation', item: siteUrl('/guide') }]),
        { '@type': 'ListItem', position: path === '/guide' ? 2 : 3, name: title, item: url },
    ]);
    const structured = $derived({
        '@context': 'https://schema.org',
        '@graph': [
            ...(path === '/' ? [{
                '@type': 'WebSite', '@id': `${SITE_ORIGIN}/#website`,
                url: siteUrl('/'), name: SITE_NAME, alternateName: 'Tide', description, inLanguage: 'en',
            }] : []),
            {
                '@type': 'WebPage', '@id': `${url}#webpage`, url, name: title, description,
                inLanguage: 'en', isPartOf: { '@id': `${SITE_ORIGIN}/#website` },
                primaryImageOfPage: { '@type': 'ImageObject', url: image, width: 1200, height: 630 },
                ...(documentation ? { breadcrumb: { '@id': `${url}#breadcrumb` } } : {}),
            },
            ...(documentation ? [{ '@type': 'BreadcrumbList', '@id': `${url}#breadcrumb`, itemListElement: breadcrumbs }] : []),
        ],
    });
</script>

<svelte:head>
    <title>{title}</title>
    <meta name="description" content={description} />
    <link rel="canonical" href={url} />
    <meta property="og:site_name" content={SITE_NAME} />
    <meta property="og:type" content="website" />
    <meta property="og:locale" content="en_US" />
    <meta property="og:title" content={title} />
    <meta property="og:description" content={description} />
    <meta property="og:url" content={url} />
    <meta property="og:image" content={image} />
    <meta property="og:image:secure_url" content={image} />
    <meta property="og:image:type" content="image/png" />
    <meta property="og:image:width" content="1200" />
    <meta property="og:image:height" content="630" />
    <meta property="og:image:alt" content={imageAlt} />
    <meta name="twitter:card" content="summary_large_image" />
    <meta name="twitter:title" content={title} />
    <meta name="twitter:description" content={description} />
    <meta name="twitter:image" content={image} />
    <meta name="twitter:image:alt" content={imageAlt} />
    {@html `<script type="application/ld+json">${jsonLd(structured)}</script>`}
</svelte:head>
