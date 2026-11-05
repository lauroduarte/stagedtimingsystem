<script lang="ts">
	import { onMount } from 'svelte';
	import { modals } from 'svelte-modals';
	import { fly } from 'svelte/transition';
	import InputPassword from '$lib/components/InputPassword.svelte';
	import Cancel from '~icons/tabler/x';
	import Save from '~icons/tabler/device-floppy';

	// provided by <Modals />

	interface Props {
		isOpen: boolean;
		title?: string;
		onSaveUser?: any;
		user?: any;
	}

	let {
		isOpen,
		title = '',
		onSaveUser = () => {},
		user: initialUser = {
			username: '',
			password: '',
			admin: false
		}
	}: Props = $props();

	let user = $state({
		username: initialUser.username ?? '',
		password: initialUser.password ?? '',
		admin: Boolean(initialUser.admin)
	});

	let errorUsername = $state(false);

	let usernameEditable = $state(false);

	onMount(() => {
		if (user.username == '') {
			usernameEditable = true;
		}
	});

	function handleSave() {
		// Validate if username is within range
		if (user.username.length < 3 || user.username.length > 32) {
			errorUsername = true;
		} else {
			errorUsername = false;
			// Callback on saving
			onSaveUser(user);
		}
	}

	function preventDefault(fn: (event: Event) => void) {
		return function (this: unknown, event: Event) {
			event.preventDefault();
			fn.call(this, event);
		};
	}
</script>

{#if isOpen}
	<div
		role="dialog"
		class="pointer-events-none fixed inset-0 z-50 flex items-center justify-center p-3 sm:p-4 overflow-y-auto"
		transition:fly={{ y: 50 }}
	>
		<div
			class="rounded-box bg-base-100 shadow-secondary/30 pointer-events-auto flex w-full max-w-md flex-col justify-between p-4 sm:p-6 shadow-xl border border-base-200"
		>
			<h2 class="text-base-content text-start text-xl sm:text-2xl font-bold">{title}</h2>
			<div class="divider my-2"></div>
			<form
				class="fieldset text-base-content mb-1 w-full space-y-3"
				onsubmit={preventDefault(handleSave)}
				novalidate
			>
				<div>
					<label class="label font-medium" for="username">Nome de Usuário</label>
					<input
						type="text"
						min="3"
						max="32"
						class="input input-bordered invalid:border-error w-full invalid:border-2"
						bind:value={user.username}
						id="username"
						disabled={!usernameEditable}
					/>
					{#if errorUsername}
						<label for="username" class="label">
							<span class="text-error text-xs">O nome de usuário deve ter entre 3 e 32 caracteres.</span>
						</label>
					{/if}
				</div>

				<div>
					<label class="label font-medium" for="pwd">Senha</label>
					<InputPassword bind:value={user.password} id="pwd" />
				</div>

				<label class="label cursor-pointer justify-start gap-3 mt-3">
					<input type="checkbox" bind:checked={user.admin} class="checkbox checkbox-primary" />
					<span class="font-medium">Administrador?</span>
				</label>

				<div class="divider my-2"></div>
				<div class="flex justify-end gap-2 pt-2">
					<button
						class="btn btn-ghost btn-sm sm:btn-md inline-flex items-center"
						onclick={() => {
							modals.close();
						}}
						type="button"
					>
						<Cancel class="mr-1.5 h-4 w-4" /><span>Cancelar</span>
					</button>
					<button
						class="btn btn-primary text-primary-content btn-sm sm:btn-md inline-flex items-center"
						type="submit"
					>
						<Save class="mr-1.5 h-4 w-4" /><span>Salvar</span>
					</button>
				</div>
			</form>
		</div>
	</div>
{/if}
