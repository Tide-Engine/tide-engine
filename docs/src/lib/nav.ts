// The site's navigation. Every page is in the sidebar, in reading order, and
// the build fails on a Markdown file that isn't (see server/content.ts). A
// page's path is its file's, without `.md`; `index.md` is its folder's.

export interface Link {
    text: string;
    link: string;
}

export interface Group {
    text: string;
    items: Link[];
}

// The header's one link: the sidebar's groups are all the docs', which start
// at the guide.
export const docs: Link = { text: 'Docs', link: '/guide' };

// A group is one subject, and its pages are in a folder of that subject:
// `game/` is what a game is made of, the match (Simulation) and what one
// machine shows of it (Presentation), `packages/` collects optional packages
// and how to use them, and `language/` is the language those are written in.
export const sidebar: Group[] = [
    {
        text: 'Get started',
        items: [
            { text: 'Introduction', link: '/guide' },
            { text: 'Install', link: '/guide/install' },
            { text: 'Editor setup', link: '/tools/editors' },
            { text: 'Your first game', link: '/guide/first-game' },
            { text: 'Using Tide with AI', link: '/guide/ai' },
            { text: 'Quick reference', link: '/guide/ai-context' },
            { text: 'Playground', link: '/guide/playground' },
            { text: 'Samples', link: '/guide/samples' },
        ],
    },
    {
        text: 'Simulation',
        items: [
            { text: 'Components and entities', link: '/game/entities' },
            { text: 'Systems', link: '/game/systems' },
            { text: 'The schedule', link: '/game/schedule' },
            { text: 'Events', link: '/game/events' },
            { text: 'Commands', link: '/game/commands' },
            { text: 'Assets', link: '/game/assets' },
            { text: 'Scenes', link: '/game/scenes' },
            { text: 'State visibility', link: '/game/visibility' },
            { text: 'Async and tasks', link: '/game/tasks' },
            { text: 'Input', link: '/game/input' },
        ],
    },
    {
        text: 'Presentation',
        items: [
            { text: 'Views and drawing', link: '/game/views' },
            { text: 'Views and the match', link: '/game/views-and-the-match' },
            { text: 'Draw settings', link: '/game/draw-settings' },
            { text: 'Meshes and textures', link: '/game/meshes-and-textures' },
            { text: 'Drawing in 3D', link: '/game/3d' },
            { text: 'Skinned meshes and animation', link: '/game/animation' },
            { text: 'Cameras and coordinates', link: '/game/cameras' },
            { text: 'Render targets', link: '/game/render-targets' },
            { text: 'Frags and verts', link: '/game/shaders' },
            { text: 'Audio', link: '/game/audio' },
            { text: 'Local state', link: '/game/local-state' },
            { text: 'Files and directories', link: '/game/files' },
            { text: 'GUI', link: '/game/gui' },
        ],
    },
    {
        text: 'Packages',
        items: [
            { text: 'Overview', link: '/packages' },
            { text: 'Using and writing packages', link: '/packages/using-and-writing' },
            { text: 'Box2D', link: '/packages/box2d' },
            { text: 'Box3D', link: '/packages/box3d' },
            { text: 'Newton', link: '/packages/newton' },
            { text: 'Rooms', link: '/packages/rooms' },
            { text: 'Lobby', link: '/packages/lobby' },
            { text: 'Steam', link: '/packages/steam' },
            { text: 'Voice chat', link: '/packages/voice' },
        ],
    },
    {
        text: 'Multiplayer',
        items: [
            { text: 'Matches', link: '/multiplayer/matches' },
            { text: 'Managed hosting', link: '/multiplayer/hosting' },
            { text: 'Shards', link: '/multiplayer/shards' },
            { text: 'Your own network and service', link: '/multiplayer/own-network' },
            { text: 'Networking', link: '/multiplayer/networking' },
            { text: 'Determinism', link: '/multiplayer/determinism' },
        ],
    },
    {
        text: 'Language',
        items: [
            { text: 'Basics', link: '/language/basics' },
            { text: 'Structs and functions', link: '/language/functions' },
            { text: 'Errors', link: '/language/errors' },
            { text: 'Math', link: '/language/math' },
            { text: 'Text and lists', link: '/language/text-and-lists' },
            { text: 'JSON', link: '/language/json' },
            { text: 'Grids', link: '/language/grids' },
            { text: 'Namespaces and files', link: '/language/namespaces' },
            { text: 'Virtual and override', link: '/language/overrides' },
        ],
    },
    {
        text: 'Tools',
        items: [
            { text: 'The tide command', link: '/tools/cli' },
            { text: 'Debug views', link: '/tools/debug-views' },
            { text: 'Usage data', link: '/tools/telemetry' },
        ],
    },
    {
        text: 'C',
        items: [
            { text: 'Calling C', link: '/c/functions' },
            { text: 'C hosts', link: '/c/hosts' },
            { text: "A game's own network", link: '/c/network' },
        ],
    },
    {
        text: 'Reference',
        items: [{ text: 'Language spec', link: '/spec' }],
    },
];

/** Every page, in reading order. */
export const pages: Link[] = sidebar.flatMap((group) => group.items);

/** The paths of the pages, without their leading `/`, as routes see them. */
export const slugs = new Set(pages.map((page) => page.link.slice(1)));
