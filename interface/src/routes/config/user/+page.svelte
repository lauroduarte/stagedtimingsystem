<script lang="ts">
	import type { PageData } from './$types';
	import { goto } from '$app/navigation';
	import { modals } from 'svelte-modals';
	import { slide } from 'svelte/transition';
	import { cubicOut } from 'svelte/easing';
	import { user } from '$lib/stores/user';
	import type { userProfile } from '$lib/stores/user';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import InputPassword from '$lib/components/InputPassword.svelte';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import ConfirmDialog from '$lib/components/ConfirmDialog.svelte';
	import EditUser from './EditUser.svelte';
	import Spinner from '$lib/components/Spinner.svelte';
	import Delete from '~icons/tabler/trash';
	import AddUser from '~icons/tabler/user-plus';
	import Edit from '~icons/tabler/pencil';
	import Admin from '~icons/tabler/key';
	import Users from '~icons/tabler/users';
	import Warning from '~icons/tabler/alert-triangle';
	import Cancel from '~icons/tabler/x';
	import Check from '~icons/tabler/check';

	interface Props {
		data: PageData;
	}

	let { data }: Props = $props();

	type userSetting = {
		username: string;
		password: string;
		admin: boolean;
	};

	type SecuritySettings = {
		jwt_secret: string;
		users: userSetting[];
	};

	let securitySettings = $state<SecuritySettings | null>(null);

	async function getSecuritySettings() {
		try {
			const response = await fetch('/rest/securitySettings', {
				method: 'GET',
				headers: {
					Authorization: page.data?.features?.security ? 'Bearer ' + $user.bearer_token : 'Basic',
					'Content-Type': 'application/json'
				}
			});
			if (response.status === 200) {
				securitySettings = await response.json();
			} else {
				notifications.error('Não autorizado ou erro ao carregar configurações de segurança.', 4000);
			}
		} catch (error) {
			console.error('Error ao carregar configurações de segurança:', error);
		}
		return;
	}

	async function postSecuritySettings(dados: SecuritySettings) {
		try {
			const response = await fetch('/rest/securitySettings', {
				method: 'POST',
				headers: {
					Authorization: page.data?.features?.security ? 'Bearer ' + $user.bearer_token : 'Basic',
					'Content-Type': 'application/json'
				},
				body: JSON.stringify(dados)
			});

			if (response.status === 200) {
				securitySettings = await response.json();
				if (await validateUser($user)) {
					notifications.success('Configurações de segurança atualizadas com sucesso.', 3000);
				}
			} else {
				notifications.error('Usuário não autorizado.', 3000);
			}
		} catch (error) {
			console.error('Erro ao salvar segurança:', error);
			notifications.error('Erro de conexão ao salvar configurações.', 3000);
		}
		return;
	}

	async function validateUser(userdata: userProfile) {
		try {
			const response = await fetch('/rest/verifyAuthorization', {
				method: 'GET',
				headers: {
					Authorization: 'Bearer ' + userdata.bearer_token,
					'Content-Type': 'application/json'
				}
			});
			if (response.status !== 200) {
				user.invalidate();
				return false;
			}
		} catch (error) {
			console.error('Error:', error);
		}
		return true;
	}

	function confirmDelete(index: number) {
		if (!securitySettings || !securitySettings.users[index]) return;
		const targetName = securitySettings.users[index].username;
		modals.open(ConfirmDialog, {
			title: 'Confirmar exclusão de usuário',
			message: `Tem certeza que deseja excluir o usuário "${targetName}"?`,
			labels: {
				cancel: { label: 'Cancelar', icon: Cancel },
				confirm: { label: 'Excluir', icon: Check }
			},
			onConfirm: () => {
				if (!securitySettings) return;
				securitySettings.users.splice(index, 1);
				modals.close();
				postSecuritySettings(securitySettings);
			}
		});
	}

	function handleEdit(index: number) {
		if (!securitySettings || !securitySettings.users[index]) return;
		modals.open(EditUser, {
			title: 'Editar Usuário',
			user: { ...securitySettings.users[index] },
			onSaveUser: (editedUser: userSetting) => {
				if (!securitySettings) return;
				securitySettings.users[index] = editedUser;
				modals.close();
				postSecuritySettings(securitySettings);
			}
		});
	}

	function handleNewUser() {
		modals.open(EditUser, {
			title: 'Adicionar Usuário',
			onSaveUser: (newUser: userSetting) => {
				if (!securitySettings) return;
				securitySettings.users = [...securitySettings.users, newUser];
				modals.close();
				postSecuritySettings(securitySettings);
			}
		});
	}
</script>

{#if $user.admin}
	<div class="w-full min-w-0 max-w-5xl mx-auto px-1 sm:px-4 py-2 sm:py-6 space-y-4 sm:space-y-6">
		<SettingsCard collapsible={false}>
			{#snippet icon()}
				<Users class="flex-shrink-0 mr-2 h-6 w-6 self-end" />
			{/snippet}
			{#snippet title()}
				<span>Gerenciar Usuários</span>
			{/snippet}
			{#await getSecuritySettings()}
				<div class="p-6 flex justify-center">
					<Spinner />
				</div>
			{:then nothing}
				{#if securitySettings && Array.isArray(securitySettings.users)}
					<div class="w-full">
						<div class="flex flex-col sm:flex-row sm:items-center justify-between gap-3 mb-4">
							<span class="text-sm sm:text-base font-semibold text-base-content/80">Usuários Cadastrados</span>
							<button
								class="btn btn-primary text-primary-content btn-sm sm:btn-md flex items-center gap-2 self-start sm:self-auto"
								onclick={handleNewUser}
							>
								<AddUser class="h-4 w-4 sm:h-5 sm:w-5" />
								<span>Adicionar Usuário</span>
							</button>
						</div>

						<div class="overflow-x-auto w-full rounded-lg border border-base-200" transition:slide|local={{ duration: 300, easing: cubicOut }}>
							<table class="table w-full">
								<thead>
									<tr class="font-bold bg-base-200/50">
										<th align="left">Usuário</th>
										<th align="center">Admin</th>
										<th align="right" class="pr-4 sm:pr-8">Ações</th>
									</tr>
								</thead>
								<tbody>
									{#each securitySettings.users as item, index}
										<tr class="hover">
											<td align="left" class="font-medium text-sm sm:text-base">{item.username}</td>
											<td align="center">
												{#if item.admin}
													<span class="badge badge-secondary badge-sm sm:badge-md gap-1">
														<Admin class="h-3.5 w-3.5" />
														<span class="hidden sm:inline">Admin</span>
													</span>
												{:else}
													<span class="badge badge-ghost badge-sm sm:badge-md text-xs">Padrão</span>
												{/if}
											</td>
											<td align="right">
												<span class="my-auto inline-flex flex-row space-x-1 sm:space-x-2">
													<button
														class="btn btn-ghost btn-circle btn-sm"
														onclick={() => handleEdit(index)}
														title="Editar"
													>
														<Edit class="h-4 w-4" />
													</button>
													<button
														class="btn btn-ghost btn-circle btn-sm text-error"
														onclick={() => confirmDelete(index)}
														title="Excluir"
													>
														<Delete class="h-4 w-4" />
													</button>
												</span>
											</td>
										</tr>
									{/each}
								</tbody>
							</table>
						</div>
					</div>
					<div class="divider my-4"></div>

					<span class="pb-2 text-lg sm:text-xl font-medium">Configurações de Segurança</span>
					<div class="alert alert-warning shadow-sm my-2 text-xs sm:text-sm">
						<Warning class="h-5 w-5 shrink-0" />
						<span>
							O segredo JWT é utilizado para assinar os tokens de autenticação. Se você alterar o segredo, todos os usuários serão desconectados.
						</span>
					</div>
					<div class="form-control w-full my-2">
						<label class="label font-medium" for="secret">Segredo JWT</label>
						<InputPassword bind:value={securitySettings.jwt_secret} id="secret" />
					</div>
					<div class="mt-6 flex justify-end">
						<button class="btn btn-primary w-full sm:w-auto" onclick={() => securitySettings && postSecuritySettings(securitySettings)}>
							Salvar Configurações
						</button>
					</div>
				{:else}
					<div class="p-4 text-center text-error">
						Não foi possível obter a lista de usuários da estação.
					</div>
				{/if}
			{/await}
		</SettingsCard>
	</div>
{:else}
	<div class="mx-auto my-8 flex flex-col items-center justify-center p-6 text-center bg-base-200 rounded-box max-w-xl">
		<Warning class="h-12 w-12 text-warning mb-2" />
		<h2 class="text-xl font-bold">Acesso Restrito</h2>
		<p class="text-sm text-gray-500 mt-1 mb-4">Esta funcionalidade requer privilégios de Administrador.</p>
		<a href="/config" class="btn btn-sm btn-outline">Voltar para o Painel</a>
	</div>
{/if}
