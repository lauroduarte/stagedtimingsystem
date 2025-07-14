<script lang="ts">
	import logo from '$lib/assets/logo.png';
	import Github from '~icons/tabler/brand-github';
	import Discord from '~icons/tabler/brand-discord';
	import Stopwatch from '~icons/tabler/stopwatch';
	import Refresh from '~icons/tabler/refresh';
	import Users from '~icons/tabler/users';
	import UsersGroup from '~icons/tabler/users-group';
	import Flag from '~icons/tabler/flag';
	import Eraser from '~icons/tabler/eraser';
	import Clock from '~icons/tabler/clock';
	import Avatar from '~icons/tabler/user-circle';
	import Logout from '~icons/tabler/logout';
	import Copyright from '~icons/tabler/copyright';
	import Link from '~icons/tabler/link';
	import { page } from '$app/state';
	import { user } from '$lib/stores/user';

	let { closeMenu } = $props();

	const github = { href: 'https://github.com/' + page.data.github, active: true };

	const discord = { href: '.', active: false };

	type menuItem = {
		title: string;
		icon: ConstructorOfATypedSvelteComponent;
		href?: string;
		feature: boolean;
		active?: boolean;
		submenu?: subMenuItem[];
	};

	type subMenuItem = {
		title: string;
		icon: ConstructorOfATypedSvelteComponent;
		href: string;
		feature: boolean;
		active: boolean;
	};

	let menuItems = $state([
		{
			title: 'Prova',
			icon: Flag,
			href: '/config/prova',
			feature: true
		},
		{
			title: 'Demo',
			icon: Flag,
			href: '/config/demo',
			feature: true
		},
		{
			title: 'Categorias',
			icon: UsersGroup,
			href: '/config/categoria',
			feature: true
		},
		{
			title: 'Atletas',
			icon: Users,
			href: '/config/atleta',
			feature: true
		},
		{
			title: 'Relógio',
			icon: Clock,
			href: '/config/relogio',
			feature: true
		},
		{
			title: 'Sincronizar',
			icon: Refresh,
			href: '/config/sincronizar',
			feature: true
		},
		{
			title: 'URL Sincronização',
			icon: Link,
			href: '/config/url-sincronizacao',
			feature: true
		},
		{
			title: 'Resetar Chaveiros RFID',
			icon: Eraser,
			href: '/config/reset-rfid',
			feature: true
		},
		{
			title: 'Cronometrar Especial',
			icon: Stopwatch,
			href: '/config/cronometrar',
			feature: true
		},
		{
			title: 'Cronometrar LAPS',
			icon: Stopwatch,
			href: '/config/cronometrar-laps',
			feature: true
		},
{
			title: 'Users',
			icon: Users,
			href: '/user',
			feature: page.data.features.security && $user.admin
		}																
	] as menuItem[]);

	function setActiveMenuItem(targetTitle: string) {
		menuItems.forEach((item) => {
			item.active = item.title === targetTitle;
			item.submenu?.forEach((subItem) => {
				subItem.active = subItem.title === targetTitle;
			});
		});
		closeMenu();
	}

	$effect(() => {
		setActiveMenuItem(page.data.title);
	});
</script>

<div class="bg-base-200 text-base-content flex h-full w-80 flex-col p-4">
	<!-- Sidebar content here -->
	<a
		href="/config"
		class="rounded-box mb-4 flex items-center hover:scale-[1.02] active:scale-[0.98]"
		onclick={() => setActiveMenuItem('')}
	>
		<img src={logo} alt="Logo" class="h-12 w-12" />
		<h1 class="px-4 text-2xl font-bold">{page.data.appName}</h1>
	</a>
	<ul class="menu w-full rounded-box menu-vertical flex-nowrap overflow-y-auto">
		{#each menuItems as menuItem, i (menuItem.title)}
			{#if menuItem.feature}
				<li>
					{#if menuItem.submenu}
						<details open={menuItem.submenu.some((subItem) => subItem.active)}>
							<summary class="text-lg font-bold">
								<menuItem.icon class="h-6 w-6" />
								{menuItem.title}
							</summary>
							<ul>
								{#each menuItem.submenu as subMenuItem}
									{#if subMenuItem.feature}
										<li class="hover-bordered">
											<a
												href={subMenuItem.href}
												class:bg-base-100={subMenuItem.active}
												class="text-ml font-bold"
												onclick={() => {
													setActiveMenuItem(subMenuItem.title);
												}}><subMenuItem.icon class="h-5 w-5" />{subMenuItem.title}</a
											>
										</li>
									{/if}
								{/each}
							</ul>
						</details>
					{:else}
						<a
							href={menuItem.href}
							class:bg-base-100={menuItem.active}
							class="text-lg font-bold"
							onclick={() => {
								setActiveMenuItem(menuItem.title);
							}}><menuItem.icon class="h-6 w-6" />{menuItem.title}</a
						>
					{/if}
				</li>
			{/if}
		{/each}
	</ul>

	<div class="flex-col"></div>
	<div class="grow"></div>

	{#if page.data.features.security}
		<div class="flex items-center">
			<Avatar class="h-8 w-8" />
			<span class="grow px-4 text-xl font-bold">{$user.username}</span>
			<!-- svelte-ignore a11y_click_events_have_key_events -->
			<!-- svelte-ignore a11y_no_static_element_interactions -->
			<div
				class="btn btn-ghost"
				onclick={() => {
					user.invalidate();
				}}
			>
				<Logout class="h-8 w-8 rotate-180" />
			</div>
		</div>
	{/if}

	<div class="divider my-0"></div>
	<div class="flex items-center">
		{#if github.active}
			<a href={github.href} class="btn btn-ghost" target="_blank" rel="noopener noreferrer"
				><Github class="h-5 w-5" /></a
			>
		{/if}
		{#if discord.active}
			<a href={discord.href} class="btn btn-ghost" target="_blank" rel="noopener noreferrer"
				><Discord class="h-5 w-5" /></a
			>
		{/if}
		<div class="inline-flex grow items-center justify-end text-sm">
			<Copyright class="h-4 w-4" /><span class="px-2">{page.data.copyright}</span>
		</div>
	</div>
</div>
