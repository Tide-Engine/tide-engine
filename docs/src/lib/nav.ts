// The site's navigation. Every page is in the sidebar, in reading order, and
// the build fails on a Markdown file that isn't (see server/content.ts). A
// page's path is its file's, without `.md`; `index.md` is its folder's.

export const repo = 'https://github.com/BlenMiner/tide-engine';

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
// machine shows of it (Presentation), and `language/` is the language those
// are written in.
export const sidebar: Group[] = [
    {
        text: 'Get started',
        items: [
            { text: 'Introduction', link: '/guide' },
            { text: 'Install', link: '/guide/install' },
            { text: 'Your first game', link: '/guide/first-game' },
            { text: 'Falling sand', link: '/guide/sand' },
            { text: 'Duel', link: '/guide/duel' },
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
            { text: 'Scenes', link: '/game/scenes' },
            { text: 'Async and tasks', link: '/game/tasks' },
            { text: 'Input', link: '/game/input' },
        ],
    },
    {
        text: 'Presentation',
        items: [
            { text: 'Views and drawing', link: '/game/views' },
            { text: 'Frags and verts', link: '/game/shaders' },
            { text: 'Local state', link: '/game/local-state' },
            { text: 'Files and directories', link: '/game/files' },
            { text: 'GUI', link: '/game/gui' },
        ],
    },
    {
        text: 'Multiplayer',
        items: [
            { text: 'Matches', link: '/multiplayer/matches' },
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
            { text: 'Grids', link: '/language/grids' },
            { text: 'Namespaces and files', link: '/language/namespaces' },
        ],
    },
    {
        text: 'Tools',
        items: [
            { text: 'The tide command', link: '/tools/cli' },
            { text: 'Editors', link: '/tools/editors' },
            { text: 'Debug views', link: '/tools/debug-views' },
            { text: 'Packages', link: '/tools/packages' },
        ],
    },
    {
        text: 'C',
        items: [
            { text: 'Calling C', link: '/c/functions' },
            { text: 'C hosts', link: '/c/hosts' },
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
