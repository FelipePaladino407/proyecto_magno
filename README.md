# Proyecto Final

## Contribuciones

Como este repositorio va a ser utilizado por todos, vamos a tratar que todas las contribuciones sigan el siguiente flujo de trabajo, esto es para evitar conflictos innecesarios y que todo sea un caos:

* La rama `main` está protegida y no acepta cambios directos, por lo tanto todas las contribuciones deberían realizarse mediante *Pull Requests (PR)*.
* Cada nueva funcionalidad o conjunto de cambios debe desarrollarse en una branch (rama) propia.
* El nombre de la rama debe describir la funcionalidad implementada. Algunos ejemplos son:

  * `lcd`
  * `touchpad`
  * `wifi`

No es necesario incluir el nombre del autor en la rama.

### Flujo de Trabajo

#### 1. Actualizar la rama principal local
Antes de comenzar a trabajar, asegurate de tener una copia actualizada de la rama principal:

```bash
git checkout main
git pull origin main
```

#### 2. Crear una nueva rama
Creá una rama para la funcionalidad que vas a desarrollar:

```bash
git checkout -b lcd
```

#### 3. Hacer los cambios
Realizá los cambios necesarios en el código y verificá que funcionen correctamente.

#### 4. Registrar los cambios
Agregá los archivos modificados y creá un commit:

```bash
git add .
git commit -m "Implementa controlador LCD"
```

Es recomendable ser descriptivo con el mensaje del commit, podés hacerlo en español o en inglés que es un poco la convención.

#### 5. Actualizar la rama con los últimos cambios de `main`
Antes de crear un Pull Request, es obligatorio incorporar localmente los cambios más recientes de la rama principal, después de commitear ejecutá lo siguiente:

```bash
git checkout main
git pull origin main

git checkout lcd
git merge main
```

Si aparecen conflictos, deberán resolverse antes de continuar.

#### 6. Verificar compilación
Después de realizar el merge con `main`, el proyecto debe compilar correctamente.

Se entiende que pueden existir módulos o funcionalidades aún no utilizadas por la aplicación principal. En esos casos, se acepta que código no ejecutado pueda contener errores, siempre que la integración realizada no rompa la compilación general del proyecto.

#### 7. Subir la rama al repositorio remoto

```bash
git push origin lcd
```

#### 8. Crear el Pull Request
Una vez subida la rama:

1. Crear un Pull Request hacia `main`. Esto lo podes hacer directamente desde la página de GitHub.
2. Preferentemente completar una descripción los cambios realizados, algo breve.
3. Esperar la asignación de los reviewers que van a aceptar la PR si está todo bien o rechazarla si por alguna razón revienta todo el código.

En caso de que una PR sea rechazada, simplemente corregí lo que este mal en la misma rama y actualizás la PR.

#### Cosas que estarían buenísimas
La convención que seguimos en clase para la indentación de código es de 4 espacios, dicha indentación la hace automáticamente el formatter que usan en su IDE o editor de preferencia,
estaría ***buenísimo*** que si lo tienen configurado en 2 espacios por ejemplo, lo pasen a 4.
